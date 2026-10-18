import { createHash, createPrivateKey, createPublicKey, randomBytes, sign, verify } from 'node:crypto';
import { readFileSync } from 'node:fs';

import { FakeSkyblip } from './fake_skyblip.mjs';
import { ServerRefusal } from '#skyblip/activation';

export const VECTORS = JSON.parse(readFileSync(new URL('../../../schemas/activation.v1.vectors.json', import.meta.url), 'utf8'));

// INFO: fc 18oct26 the DER headers that wrap a raw Ed25519 seed or public key for node:crypto
const PKCS8_ED25519 = Buffer.from('302e020100300506032b657004220420', 'hex');
const SPKI_ED25519 = Buffer.from('302a300506032b6570032100', 'hex');
const REGISTRATION_FRAME_BYTES = 320;

const bytes = (...hex) => Buffer.from(hex.join(''), 'hex');
const label = purpose => Buffer.from(`skyblip:${purpose}:v1`);
const hex = buffer => Buffer.from(buffer).toString('hex');

export const messages = {
  register: ({ unitNonce, serverNonce }) => Buffer.concat([label('register'), bytes(unitNonce, serverNonce)]),
  registration: ({ deviceId, publicKey, unitNonce, serverNonce, serverKey, release, version }) =>
    Buffer.concat([label('registration'), bytes(deviceId, publicKey, unitNonce, serverNonce, serverKey), Buffer.of(release ? 1 : 0), Buffer.from(version)]),
  unlock: ({ deviceId, publicKey, serial, unitNonce }) => {
    const serialBytes = Buffer.alloc(2);
    serialBytes.writeUInt16BE(serial);
    return Buffer.concat([label('unlock'), bytes(deviceId, publicKey), serialBytes, bytes(unitNonce)]);
  },
};

export function keyPair(seed) {
  const privateKey = createPrivateKey({ key: Buffer.concat([PKCS8_ED25519, bytes(seed)]), format: 'der', type: 'pkcs8' });
  const publicKey = hex(createPublicKey(privateKey).export({ format: 'der', type: 'spki' }).subarray(SPKI_ED25519.length));
  return {
    publicKey,
    id: hex(createHash('sha256').update(bytes(publicKey)).digest().subarray(0, 4)),
    sign: message => hex(sign(null, message, privateKey)),
  };
}

export function verifies(publicKey, signature, message) {
  const key = createPublicKey({ key: Buffer.concat([SPKI_ED25519, bytes(publicKey)]), format: 'der', type: 'spki' });
  return verify(null, message, key, bytes(signature));
}

// INFO: fc 18oct26 a reading of the server's rules, as fake_skyblip.mjs is a reading of config.cpp
export class FakeServer {
  constructor({ seed = VECTORS.server_seed } = {}) {
    this.key = keyPair(seed);
    this.challenges = new Map();
    this.units = new Map();
    this.refuseNext = null;
    this.requests = [];
    this.held = null;
  }

  hold() {
    this.held = Promise.withResolvers();
  }

  release() {
    this.held.resolve();
    this.held = null;
  }

  async challenge({ unit_nonce, server_key }) {
    this.requests.push('challenge');
    this.#refuse();
    if (server_key !== this.key.id) throw new ServerRefusal('unknown_server_key');
    const serverNonce = hex(randomBytes(16));
    this.challenges.set(serverNonce, unit_nonce);
    return { server_nonce: serverNonce, signature: this.key.sign(messages.register({ unitNonce: unit_nonce, serverNonce })) };
  }

  async register({ device_id, public_key, version, server_key, release, server_nonce, signature }) {
    this.requests.push('register');
    if (this.held) await this.held.promise;
    this.#refuse();
    const unitNonce = this.challenges.get(server_nonce);
    if (!unitNonce) throw new ServerRefusal('expired');
    this.challenges.delete(server_nonce);
    const message = messages.registration({
      deviceId: device_id, publicKey: public_key, unitNonce, serverNonce: server_nonce, serverKey: server_key, release, version,
    });
    if (!verifies(public_key, signature, message)) throw new ServerRefusal('bad_signature');
    const known = this.units.get(device_id) ?? { serial: this.units.size + 1, publicKey: public_key };
    if (known.publicKey !== public_key) throw new ServerRefusal('other_key');
    this.units.set(device_id, known);
    const unlock = messages.unlock({ deviceId: device_id, publicKey: public_key, serial: known.serial, unitNonce });
    return { serial: known.serial, signature: this.key.sign(unlock) };
  }

  #refuse() {
    const key = this.refuseNext;
    this.refuseNext = null;
    if (key) throw new ServerRefusal(key);
  }
}

// INFO: fc 18oct26 the activation dialect of schemas/activation.v1.schema.json, over the fake's config endpoint
export class FakeUnit extends FakeSkyblip {
  constructor({ seed = VECTORS.unit_seed, deviceId = VECTORS.device_id, trusted = VECTORS.server_public_key,
    version = VECTORS.version, release = VECTORS.release, complete = true, ...options } = {}) {
    super({ addr: null, ...options });
    this.unitKey = keyPair(seed);
    Object.assign(this, { deviceId, trusted, version, release, complete });
    this.trustedId = hex(createHash('sha256').update(bytes(trusted)).digest().subarray(0, 4));
    this.nonce = null;
    this.serverNonce = null;
    this.unlockCode = null;
  }

  onConfig(raw) {
    const fields = JSON.parse(new TextDecoder().decode(raw));
    if (this.claimedBy !== null || !['hello', 'register', 'unlock'].includes(fields.cmd)) return super.onConfig(raw);
    this.commands.push(fields.cmd);
    if (!this.complete) return this.ack(false, 'incomplete');
    if (fields.cmd === 'hello') return this.hello();
    if (!this.nonce) return this.ack(false, 'no_session');
    if (fields.cmd === 'register') return this.register(fields);
    return this.unlock(fields);
  }

  hello() {
    this.nonce = hex(randomBytes(16));
    this.reply({ cmd: 'hello', nonce: this.nonce, server_key: this.trustedId });
  }

  register({ server_nonce, signature }) {
    if (!verifies(this.trusted, signature, messages.register({ unitNonce: this.nonce, serverNonce: server_nonce }))) {
      return this.ack(false, 'bad_signature');
    }
    if (this.mtu - 3 < REGISTRATION_FRAME_BYTES) return this.ack(false, 'small_mtu');
    this.serverNonce = server_nonce;
    this.pending = 'register';
    this.reply({ ack: false, pending: true, reason: 'confirm_register' });
  }

  press() {
    if (this.pending !== 'register') return super.press();
    this.pending = null;
    const fields = { deviceId: this.deviceId, publicKey: this.unitKey.publicKey, unitNonce: this.nonce, serverNonce: this.serverNonce,
      serverKey: this.trustedId, release: this.release, version: this.version };
    this.reply({
      cmd: 'registration', device_id: this.deviceId, public_key: this.unitKey.publicKey, version: this.version,
      server_key: this.trustedId, release: this.release, signature: this.unitKey.sign(messages.registration(fields)),
    });
  }

  unlock({ serial, signature }) {
    const nonce = this.nonce;
    this.nonce = null;
    const message = messages.unlock({ deviceId: this.deviceId, publicKey: this.unitKey.publicKey, serial, unitNonce: nonce });
    if (!verifies(this.trusted, signature, message)) return this.ack(false, 'bad_signature');
    this.addr = serial;
    this.unlockCode = { serial, nonce, signature };
    this.ack(true, 'unlock');
  }

  drop() {
    this.nonce = null;
    super.drop();
  }
}
