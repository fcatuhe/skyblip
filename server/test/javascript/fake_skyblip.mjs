import { decode, encode } from '#skyblip/cbor';
import { UUID } from '#skyblip/ble';
import { compareVersions, parseVersion, readImage, versionText } from '#skyblip/image';
import { decodeFrame, encodeFrame, hex } from '#skyblip/link';
import { signedMessage } from '#skyblip/trust';
import { PRODUCTION_KEY } from './fake_image.mjs';
import { FakeLogStore } from './fake_log.mjs';

export { PRODUCTION_KEY, imageClass, signedImage } from './fake_image.mjs';

const SMP_HEADER_BYTES = 8;
export const ATT_HEADER_BYTES = 3;
const PREPARE_HEADER_BYTES = 5;
// INFO: fc 03oct26 CONFIG_BT_ATT_PREPARE_COUNT in prj.conf: Zephyr reassembles that many segments
const PREPARE_COUNT = 2;
const MGMT_ERR_EMSGSIZE = 7;
const MGMT_ERR_ENOTSUP = 8;
const MGMT_ERR_EACCESSDENIED = 11;
const IMAGE_GROUP = 1;
export const IMG_MGMT_ERR_INVALID_TLV = 5;
export const IMG_MGMT_ERR_CURRENT_VERSION_IS_NEWER = 27;
export const DEVELOPMENT_KEY = 0xa1;
export const DEFAULTS = { aircraft_type: 1, alarm_enabled: true, alarm_volume: 'loud', units: 'nautical', callsign: '', own_icao: 0 };
export const BLOB_PIN = '9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08';
const DIGEST = /^[0-9a-f]{16}$/;

const tick = () => new Promise(resolve => setImmediate(resolve));

class Characteristic {
  constructor(link, onWrite) {
    this.link = link;
    this.onWrite = onWrite;
    this.listeners = [];
    this.notifying = false;
    this.longestAttempt = 0;
  }

  async startNotifications() {
    if (this.link.writing) this.link.overlaps++;
    this.link.writing = true;
    await tick();
    this.link.writing = false;
    this.notifying = true;
  }

  async stopNotifications() {
    this.notifying = false;
  }

  addEventListener(type, listener) {
    if (type === 'characteristicvaluechanged') this.listeners.push(listener);
  }

  removeEventListener(type, listener) {
    this.listeners = this.listeners.filter(each => each !== listener);
  }

  notify(bytes) {
    if (!this.notifying) return;
    if (bytes.length > this.link.mtu - ATT_HEADER_BYTES) {
      this.link.refusedNotifications++;
      return;
    }
    const value = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    for (const listener of this.listeners) listener({ target: { value } });
  }

  writeValueWithResponse(bytes) {
    return this.link.write(this, bytes, { long: true });
  }

  writeValueWithoutResponse(bytes) {
    return this.link.write(this, bytes);
  }
}

// INFO: fc 26sep26 defaults are the device's: 2475 is CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE in prj.conf
export class FakeSkyblip {
  constructor({
    running = '0.1.0.12',
    image = 'confirmed',
    from = null,
    to = null,
    settings = null,
    imu = BLOB_PIN.slice(0, 16),
    flight = 'ground',
    swapPowered = true,
    bufSize = 2475,
    mtu = 185,
    claimedBy = null,
    smp = true,
    echo = true,
    writeCeiling = mtu - ATT_HEADER_BYTES,
    oversize = 'truncate',
    stored = DEFAULTS,
    knowsDefaults = true,
    knowsBrowsers = true,
    verdictFirst = false,
    key = PRODUCTION_KEY,
    downgrade = false,
    address = 0x00012a,
    product = 'skyblip_go',
    name = 'skyBlip Go 5B5AFE',
    logStore = new FakeLogStore(),
    installReport = undefined,
    protocols = undefined,
  } = {}) {
    Object.assign(this, { running, image, from, to, settings, imu, flight, swapPowered, bufSize, mtu, claimedBy });
    Object.assign(this, { stored: { ...stored }, knowsDefaults, knowsBrowsers, key, downgrade, address, product });
    Object.assign(this, { echo, writeCeiling, oversize, verdictFirst, name, logStore, installReport, protocols });
    this.hasSmp = smp;
    this.writesInPacket = 0;
    this.uploadWrites = [];
    this.connected = false;
    this.pending = null;
    this.refusedOnDevice = false;
    this.staged = null;
    this.capturing = false;
    this.windowOpen = false;
    this.slot = null;
    this.finished = false;
    this.upload = null;
    this.commands = [];
    this.uploadOffsets = [];
    this.longestWrite = 0;
    this.longestAttempt = 0;
    this.longestPacket = 0;
    this.refusedNotifications = 0;
    this.linkDrops = 0;
    this.smallMtuRefusals = 0;
    this.overlaps = 0;
    this.writing = false;
    this.approved = null;
    this.refuseInstall = null;
    this.landedCheck = 'matched';
    this.landing = null;
    this.refuseUpload = null;
    this.refuseAfterPackets = Infinity;
    this.closeWindowAfterPackets = Infinity;
    this.dropAfterPackets = Infinity;
    this.held = new Uint8Array(0);
    this.listeners = [];
    this.trustedBrowsers = [];
    this.forgetSession();
  }

  forgetSession() {
    this.nonce = null;
    this.proven = null;
    this.sessionTrusted = false;
  }

  install() {
    const fake = this;
    Object.defineProperty(globalThis, 'navigator', {
      value: { bluetooth: { requestDevice: async () => fake.device() } },
      configurable: true,
      writable: true,
    });
    return this;
  }

  device() {
    const fake = this;
    this.config = new Characteristic(this, bytes => this.onConfig(bytes));
    this.smp = new Characteristic(this, bytes => this.onSmp(bytes));
    this.log = new Characteristic(this, bytes => this.onLog(bytes));
    const services = {
      [UUID.skyblip]: { [UUID.config]: this.config, [UUID.log]: this.log },
      ...(this.hasSmp ? { [UUID.smp]: { [UUID.smpChr]: this.smp } } : {}),
    };
    const server = {
      async getPrimaryService(uuid) {
        const chars = services[uuid];
        if (!chars) throw new Error(`no service ${uuid}`);
        return { getCharacteristic: async id => chars[id] };
      },
    };
    return {
      name: this.name,
      addEventListener: (type, listener) => this.listeners.push(listener),
      gatt: {
        get connected() {
          return fake.connected;
        },
        connect: async () => {
          fake.connected = true;
          fake.linkUp();
          return server;
        },
        disconnect: () => fake.drop(),
      },
    };
  }

  async write(characteristic, bytes, { long = false } = {}) {
    if (!this.connected) throw new Error('GATT Server is disconnected.');
    if (long && bytes.length > this.mtu - ATT_HEADER_BYTES) return this.longWrite(characteristic, bytes);
    if (this.writing) this.overlaps++;
    this.writing = true;
    this.longestAttempt = Math.max(this.longestAttempt, bytes.length);
    characteristic.longestAttempt = Math.max(characteristic.longestAttempt, bytes.length);
    await tick();
    this.writing = false;
    if (bytes.length > this.writeCeiling && this.oversize === 'reject') throw new Error('GATT operation failed for unknown reason.');
    const landed = Uint8Array.from(bytes.subarray(0, this.writeCeiling));
    this.longestWrite = Math.max(this.longestWrite, landed.length);
    characteristic.onWrite(landed);
  }

  async longWrite(characteristic, bytes) {
    const segment = this.mtu - PREPARE_HEADER_BYTES;
    if (this.writing) this.overlaps++;
    this.writing = true;
    this.longestAttempt = Math.max(this.longestAttempt, Math.min(bytes.length, segment));
    await tick();
    this.writing = false;
    if (Math.ceil(bytes.length / segment) > PREPARE_COUNT) throw new Error('GATT Error: Prepare queue full.');
    this.longestWrite = Math.max(this.longestWrite, Math.min(bytes.length, segment));
    characteristic.onWrite(Uint8Array.from(bytes));
  }

  drop() {
    if (!this.connected) return;
    this.connected = false;
    this.pending = null;
    this.refusedOnDevice = false;
    this.windowOpen = false;
    this.forgetSession();
    for (const listener of this.listeners) listener();
  }

  linkUp() {
    setImmediate(() => {
      if (this.image !== 'confirmed' || this.settings) this.sendUpdate({ asked: false });
    });
  }

  reply(fields, { asked = true } = {}) {
    const frame = encodeFrame(fields);
    setImmediate(() => {
      if (!this.connected) return;
      if (frame.length <= this.mtu - ATT_HEADER_BYTES) return this.config.notify(frame);
      this.linkDrops++;
      if (!asked) return;
      this.smallMtuRefusals++;
      this.config.notify(encodeFrame({ command: 'acknowledgement', acknowledge: false, reason: 'small_mtu' }));
    });
  }

  acknowledge(ok, reason) {
    this.reply({ command: 'acknowledgement', acknowledge: ok, reason });
  }

  onConfig(bytes) {
    this.answer(decodeFrame(bytes));
  }

  onLog(bytes) {
    for (const reply of this.logStore.answer(decodeFrame(bytes), this.mtu - ATT_HEADER_BYTES)) {
      setImmediate(() => this.connected && this.log.notify(encodeFrame(reply)));
    }
  }

  answer({ command, ...fields }) {
    this.commands.push(command);
    if (this.claimedBy !== null) return this.reply({ command: 'acknowledgement', acknowledge: false, reason: 'claimed', by: this.claimedBy });
    if (command === 'update') return this.sendUpdate();
    if (command === 'status') return this.sendStatus();
    if (command === 'get') return this.reply({ command: 'config', version: 1, ...this.addressFields(), ...this.stored });
    if (command === 'defaults' && this.knowsDefaults) return this.reply({ command: 'defaults', ...DEFAULTS });
    if (command === 'capture' && this.product === 'skyblip_bravo') return this.stageCapture(fields);
    // INFO: fc 13nov26 undefined is firmware older than the install report, which answers unknown_command
    if (command === 'install_report' && this.installReport !== undefined) return this.reply({ command, ...this.installReport, ...this.landing });
    // INFO: fc 14nov26 undefined is firmware older than the protocols frame, which answers unknown_command
    if (command === 'protocols' && this.protocols !== undefined) return this.reply({ command, ...this.protocols });
    if (BROWSER_COMMANDS.has(command) && this.knowsBrowsers) return this.answerBrowser(command, fields);
    if (!['set', 'dfu', 'recovery'].includes(command)) return this.acknowledge(false, 'unknown_command');
    if (this.refusedOnDevice) return this.acknowledge(false, 'refused_on_device');
    if (this.flight === 'airborne' && !(command === 'set' && this.sessionTrusted)) return this.acknowledge(false, 'in_flight');
    if (command !== 'recovery' && !this.swapPowered) return this.acknowledge(false, 'low_power');
    if (command === 'dfu' && !fields.version) return this.acknowledge(false, 'no_version');
    if (command === 'dfu' && this.older(parseVersion(fields.version))) return this.acknowledge(false, 'older');
    if (command === 'dfu') this.approved = fields.version;
    this.pending = command;
    if (command === 'set') this.staged = fields;
    if (this.sessionTrusted && command !== 'recovery') return this.carryOut();
    if (command === 'set') return this.reply({ command: 'acknowledgement', acknowledge: false, pending: true, reason: 'confirm' });
    this.reply({ command: 'acknowledgement', acknowledge: false, pending: true, reason: `confirm_${command}` });
  }

  // INFO: fc 13nov26 browser_link.cpp's order: the nonce, the label, then the signature
  async answerBrowser(command, fields) {
    if (command === 'browser_challenge') {
      this.forgetSession();
      this.nonce = crypto.getRandomValues(new Uint8Array(16));
      return this.reply({ command, nonce: this.nonce });
    }
    if (command === 'trust_browser') {
      const nonce = this.nonce;
      this.nonce = null;
      if (!nonce) return this.acknowledge(false, 'no_session');
      if (new TextEncoder().encode(fields.label ?? '').length > 24) return this.acknowledge(false, 'invalid');
      const key = await crypto.subtle.importKey('raw', fields.key, { name: 'Ed25519' }, false, ['verify']);
      if (!(await crypto.subtle.verify({ name: 'Ed25519' }, key, fields.signature, signedMessage(nonce)))) return this.acknowledge(false, 'bad_signature');
      this.proven = { id: hex(fields.key.subarray(0, 8)), label: fields.label ?? '' };
      this.sessionTrusted = this.trustedBrowsers.some(({ id }) => id === this.proven.id);
      return this.acknowledge(this.sessionTrusted, this.sessionTrusted ? 'trusted' : 'untrusted');
    }
    if (!this.sessionTrusted) return this.acknowledge(false, 'untrusted');
    if (command === 'browsers') {
      const entry = this.trustedBrowsers[fields.index];
      const found = entry ? { id: entry.id, label: entry.label } : {};
      return this.reply({ command: 'browser', index: fields.index, count: this.trustedBrowsers.length, ...found });
    }
    const id = hex(fields.id);
    const before = this.trustedBrowsers.length;
    this.trustedBrowsers = this.trustedBrowsers.filter(entry => entry.id !== id);
    if (this.trustedBrowsers.length === before) return this.acknowledge(false, 'unknown_browser');
    if (id === this.proven?.id) this.forgetSession();
    this.acknowledge(true, 'forgotten');
  }

  trust(browser) {
    if (this.trustedBrowsers.some(({ id }) => id === browser.id)) return;
    this.trustedBrowsers = [...this.trustedBrowsers, browser].slice(-8);
    this.sessionTrusted = true;
  }

  // INFO: fc 12nov26 capture_request.cpp's order: the prompt guards, the flight, then the capture's state
  stageCapture({ profile }) {
    if (this.refusedOnDevice) return this.acknowledge(false, 'refused_on_device');
    if (this.flight === 'airborne') return this.acknowledge(false, 'in_flight');
    const stop = profile === 'stop';
    if (this.capturing && !stop) return this.acknowledge(false, 'capturing');
    if (!this.capturing && stop) return this.acknowledge(false, 'not_capturing');
    this.pending = 'capture';
    this.staged = profile;
    this.reply({ command: 'acknowledgement', acknowledge: false, pending: true, reason: 'confirm_capture' });
  }

  addressFields() {
    return { ...(this.address === null ? {} : { address: this.address }), address_mapping_table: 58 };
  }

  takeOff() {
    this.flight = 'airborne';
    this.windowOpen = false;
    if (this.pending) {
      this.pending = null;
      this.acknowledge(false, 'in_flight');
    }
    this.sendStatus();
  }

  land() {
    this.flight = 'ground';
    this.sendStatus();
  }

  installLanded() {
    this.windowOpen = false;
    const image = readImage(this.slot);
    const refusal = this.landingRefusal(image);
    if (refusal) {
      if (LANDING_REFUSALS.has(refusal)) this.landing = { landed_check: this.landedCheck, ...(image?.key && { staged_key: image.key }) };
      return this.acknowledge(false, refusal);
    }
    this.acknowledge(true, 'install');
    setImmediate(() => setImmediate(() => this.drop()));
  }

  landingRefusal(image) {
    if (this.refuseInstall) return this.refuseInstall;
    if (!image || versionText(image.version) !== this.approved) return 'not_approved';
    if (this.key !== null && image.key !== keyName(this.key)) return 'wrong_key';
    return null;
  }

  older(version) {
    return !this.downgrade && compareVersions(version, parseVersion(this.running)) < 0;
  }

  sendUpdate({ asked = true } = {}) {
    const frame = { command: 'update', image: this.image };
    if (this.image !== 'confirmed') Object.assign(frame, { from: this.from, to: this.to });
    if (this.settings) frame.settings = this.settings;
    if (DIGEST.test(this.imu)) Object.assign(frame, { imu: 'held', imu_digest: this.imu });
    else frame.imu = this.imu;
    frame.swap_powered = this.swapPowered;
    if (this.downgrade) frame.downgrade = true;
    if (this.key !== null) frame.key = keyName(this.key);
    if (this.product !== null && this.product !== 'skyblip_go') frame.product = this.product;
    this.reply(frame, { asked });
  }

  sendStatus() {
    this.reply({
      command: 'status',
      flight: this.flight,
      upload: this.windowOpen && this.flight !== 'airborne',
      battery_percent: this.swapPowered ? 80 : 12,
      charging: false,
      power_level: this.swapPowered ? 'normal' : 'low',
      went_dark_flat: false,
      die_temperature_c: 21,
    });
  }

  press() {
    const remembers = ['set', 'dfu'].includes(this.pending) && this.proven;
    this.carryOut();
    if (remembers) this.trust(this.proven);
  }

  carryOut() {
    const task = this.pending;
    this.pending = null;
    if (task === 'set') {
      Object.assign(this.stored, this.staged);
      return this.reply({ command: 'acknowledgement', acknowledge: true });
    }
    if (task === 'capture') {
      this.capturing = this.staged !== 'stop';
      return this.acknowledge(true, 'capture');
    }
    if (task === 'dfu') {
      this.windowOpen = true;
      this.landing = null;
      this.finished = false;
      return this.acknowledge(true, 'dfu');
    }
    this.acknowledge(true, task);
    setImmediate(() => setImmediate(() => this.drop()));
  }

  refuse() {
    if (this.pending) this.refusedOnDevice = true;
    this.pending = null;
    this.windowOpen = false;
    this.acknowledge(false, 'cancelled');
  }

  expire() {
    this.pending = null;
    this.acknowledge(false, 'expired');
  }

  push(fields) {
    this.reply(fields, { asked: false });
  }

  onSmp(bytes) {
    this.writesInPacket++;
    const joined = new Uint8Array(this.held.length + bytes.length);
    joined.set(this.held);
    joined.set(bytes, this.held.length);
    this.held = joined;
    if (this.held.length < SMP_HEADER_BYTES) return;
    const end = SMP_HEADER_BYTES + ((this.held[2] << 8) | this.held[3]);
    if (this.held.length < end) return;
    const request = this.held.slice(0, end);
    this.held = this.held.slice(end);
    this.longestPacket = Math.max(this.longestPacket, request.length);
    if (request[7] === 1 && ((request[4] << 8) | request[5]) === 1) this.uploadWrites.push(this.writesInPacket);
    this.writesInPacket = 0;
    this.answerSmp(request);
  }

  answerSmp(request) {
    const op = request[0] & 0x07;
    const group = (request[4] << 8) | request[5];
    const id = request[7];
    const body = decode(request.subarray(SMP_HEADER_BYTES));
    const upload = group === 1 && id === 1 && op === 2;
    let answer;
    if (request.length > this.bufSize) answer = { rc: MGMT_ERR_EMSGSIZE };
    else if (group === 0 && id === 0) answer = this.echo ? { r: body.d } : { rc: MGMT_ERR_ENOTSUP };
    else if (group === 0 && id === 6) answer = { buf_size: this.bufSize, buf_count: 4 };
    else if (group === 1 && id === 0) answer = { images: [{ slot: 0, version: this.running, active: true, confirmed: true }] };
    else if (upload) answer = this.imageUpload(body);
    else answer = { rc: MGMT_ERR_ENOTSUP };
    this.sendSmp(request, answer);
    if (upload && --this.dropAfterPackets === 0) setImmediate(() => this.drop());
    if (upload && --this.refuseAfterPackets === 0) this.refuse();
  }

  imageUpload({ off, len, sha, data }) {
    if (!this.windowOpen || this.flight === 'airborne') return { rc: MGMT_ERR_EACCESSDENIED };
    if (--this.closeWindowAfterPackets === 0) this.windowOpen = false;
    this.uploadOffsets.push(off);
    if (off === 0) {
      if (this.upload && sha && this.upload.sha.join() === sha.join()) return { off: this.upload.off };
      if (this.refuseUpload) return { err: { group: IMAGE_GROUP, rc: this.refuseUpload } };
      const header = readImage(data);
      if (header && this.older(header.version)) return { err: { group: IMAGE_GROUP, rc: IMG_MGMT_ERR_CURRENT_VERSION_IS_NEWER } };
      this.upload = { sha, len, off: 0, classEnd: classEndOf(data) };
      this.slot = new Uint8Array(len);
      this.finished = false;
    }
    if (!this.upload || off !== this.upload.off) return { off: this.upload ? this.upload.off : 0 };
    if (this.refusesProduct(data, off)) return { err: { group: IMAGE_GROUP, rc: IMG_MGMT_ERR_INVALID_TLV } };
    this.slot.set(data, off);
    this.upload.off += data.length;
    const reached = this.upload.off;
    if (reached === this.upload.len) {
      this.finished = true;
      this.upload = null;
      if (this.verdictFirst) this.installLanded();
      else setImmediate(() => this.installLanded());
    }
    return { off: reached };
  }

  // INFO: fc 10nov26 a null product is firmware before the check, which takes any image its key signed
  refusesProduct(data, off) {
    if (this.product === null) return false;
    const { classEnd, len } = this.upload;
    if (classEnd === null || classEnd > len) return true;
    if (off >= classEnd || off + data.length < classEnd) return false;
    return readImage(this.slotWith(data, off)).product !== this.product;
  }

  slotWith(data, off) {
    const slot = Uint8Array.from(this.slot);
    slot.set(data, off);
    return slot;
  }

  sendSmp(request, answer) {
    const payload = indefiniteMap(answer);
    const packet = new Uint8Array(SMP_HEADER_BYTES + payload.length);
    packet.set(request.subarray(0, SMP_HEADER_BYTES));
    packet[0] = (request[0] & ~0x07) | ((request[0] & 0x07) + 1);
    packet[2] = payload.length >> 8;
    packet[3] = payload.length & 0xff;
    packet.set(payload, SMP_HEADER_BYTES);
    const fragment = this.mtu - ATT_HEADER_BYTES;
    setImmediate(() => {
      for (let at = 0; at < packet.length && this.connected; at += fragment) this.smp.notify(packet.slice(at, at + fragment));
    });
  }
}

// INFO: fc 26sep26 zcbor opens every map as indefinite-length, so the fake answers the same way
function indefiniteMap(object) {
  const parts = [Uint8Array.of(0xbf)];
  for (const [key, value] of Object.entries(object)) parts.push(encode(key), encode(value));
  parts.push(Uint8Array.of(0xff));
  const out = new Uint8Array(parts.reduce((sum, part) => sum + part.length, 0));
  let at = 0;
  for (const part of parts) {
    out.set(part, at);
    at += part.length;
  }
  return out;
}

function classEndOf(header) {
  const view = new DataView(header.buffer, header.byteOffset, header.byteLength);
  const protectedBytes = view.getUint16(10, true);
  return protectedBytes === 0 ? null : view.getUint16(8, true) + view.getUint32(12, true) + protectedBytes;
}

const LANDING_REFUSALS = new Set(['nothing_staged', 'not_approved', 'wrong_key']);
const BROWSER_COMMANDS = new Set(['browser_challenge', 'trust_browser', 'browsers', 'forget_browser']);

function keyName(byte) {
  return byte.toString(16).padStart(2, '0').repeat(4).toUpperCase();
}
