import { decode, encode } from './cbor.js';
import { UUID } from './ble.js';
import { readImage, versionText } from './image.js';

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const SMP_HEADER_BYTES = 8;
const ATT_HEADER_BYTES = 3;
const PREPARE_HEADER_BYTES = 5;
// INFO: fc 03oct26 CONFIG_BT_ATT_PREPARE_COUNT in prj.conf: Zephyr reassembles that many segments
const PREPARE_COUNT = 2;
const MGMT_ERR_EMSGSIZE = 7;
const MGMT_ERR_ENOTSUP = 8;
const MGMT_ERR_EACCESSDENIED = 11;
const IMAGE_GROUP = 1;
export const IMG_MGMT_ERR_CURRENT_VERSION_IS_NEWER = 27;
export const DEFAULTS = { aircraft_type: 1, alarm: true, alarm_volume: 3, units: 0, callsign: '' };

const tick = () => new Promise(resolve => setImmediate(resolve));

class Characteristic {
  constructor(link, onWrite) {
    this.link = link;
    this.onWrite = onWrite;
    this.listeners = [];
    this.longestAttempt = 0;
  }

  async startNotifications() {}

  addEventListener(type, listener) {
    if (type === 'characteristicvaluechanged') this.listeners.push(listener);
  }

  notify(bytes) {
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
    onGround = true,
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
    verdictFirst = false,
  } = {}) {
    Object.assign(this, { running, image, from, to, settings, onGround, swapPowered, bufSize, mtu, claimedBy });
    Object.assign(this, { stored: { ...stored }, knowsDefaults });
    Object.assign(this, { echo, writeCeiling, oversize, verdictFirst });
    this.hasSmp = smp;
    this.writesInPacket = 0;
    this.uploadWrites = [];
    this.connected = false;
    this.pending = null;
    this.staged = null;
    this.windowOpen = false;
    this.slot = null;
    this.finished = false;
    this.upload = null;
    this.commands = [];
    this.uploadOffsets = [];
    this.longestWrite = 0;
    this.longestAttempt = 0;
    this.longestPacket = 0;
    this.overlaps = 0;
    this.writing = false;
    this.approved = null;
    this.refuseInstall = null;
    this.refuseUpload = null;
    this.refuseAfterPackets = Infinity;
    this.closeWindowAfterPackets = Infinity;
    this.dropAfterPackets = Infinity;
    this.held = new Uint8Array(0);
    this.listeners = [];
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
    const services = {
      [UUID.skyblip]: { [UUID.config]: this.config },
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
      name: 'skyBlip Go 5B5AFE',
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
    this.windowOpen = false;
    for (const listener of this.listeners) listener();
  }

  linkUp() {
    setImmediate(() => {
      if (this.image !== 'confirmed' || this.settings) this.sendUpdate();
    });
  }

  reply(json) {
    setImmediate(() => this.connected && this.config.notify(encoder.encode(JSON.stringify(json))));
  }

  ack(ok, reason) {
    this.reply({ ack: ok, reason });
  }

  onConfig(bytes) {
    const { cmd, ...fields } = JSON.parse(decoder.decode(bytes));
    this.commands.push(cmd);
    if (this.claimedBy !== null) return this.reply({ ack: false, reason: 'claimed', by: this.claimedBy });
    if (cmd === 'update') return this.sendUpdate();
    if (cmd === 'status') return this.sendStatus();
    if (cmd === 'get') return this.reply({ cmd: 'config', version: 1, addr: 0x5b5afe, addr_table: 58, ...this.stored });
    if (cmd === 'defaults' && this.knowsDefaults) return this.reply({ cmd: 'defaults', ...DEFAULTS });
    if (!['set', 'dfu', 'recovery'].includes(cmd)) return this.ack(false, 'unknown_cmd');
    if (!this.onGround) return this.ack(false, 'in_flight');
    if (cmd !== 'recovery' && !this.swapPowered) return this.ack(false, 'low_power');
    if (cmd === 'dfu' && !fields.version) return this.ack(false, 'no_version');
    if (cmd === 'dfu') this.approved = fields.version;
    this.pending = cmd;
    if (cmd === 'set') {
      this.staged = fields;
      return this.reply({ ack: false, pending: true, reason: 'confirm' });
    }
    this.reply({ ack: false, pending: true, reason: `confirm_${cmd}` });
  }

  takeOff() {
    this.onGround = false;
    this.windowOpen = false;
    if (this.pending) {
      this.pending = null;
      this.ack(false, 'in_flight');
    }
    this.sendStatus();
  }

  land() {
    this.onGround = true;
    this.sendStatus();
  }

  installLanded() {
    this.windowOpen = false;
    const image = readImage(this.slot);
    if (this.refuseInstall) return this.ack(false, this.refuseInstall);
    if (!image || versionText(image.version) !== this.approved) return this.ack(false, 'not_approved');
    this.ack(true, 'install');
    setImmediate(() => setImmediate(() => this.drop()));
  }

  sendUpdate() {
    const frame = { cmd: 'update', image: this.image };
    if (this.image !== 'confirmed') Object.assign(frame, { from: this.from, to: this.to });
    if (this.settings) frame.settings = this.settings;
    frame.swap_powered = this.swapPowered;
    this.reply(frame);
  }

  sendStatus() {
    this.reply({
      cmd: 'status',
      flight: this.onGround ? 'ground' : 'airborne',
      upload: this.windowOpen && this.onGround,
      battery_percent: this.swapPowered ? 80 : 12,
      charging: false,
      power_level: this.swapPowered ? 'OK' : 'LOW',
      went_dark_flat: false,
      die_temp_c: 21,
    });
  }

  press() {
    const task = this.pending;
    this.pending = null;
    if (task === 'set') {
      Object.assign(this.stored, this.staged);
      return this.reply({ ack: true });
    }
    if (task === 'dfu') {
      this.windowOpen = true;
      this.finished = false;
      return this.ack(true, 'dfu');
    }
    this.ack(true, task);
    setImmediate(() => setImmediate(() => this.drop()));
  }

  refuse() {
    this.pending = null;
    this.windowOpen = false;
    this.ack(false, 'cancelled');
  }

  expire() {
    this.pending = null;
    this.ack(false, 'expired');
  }

  push(json) {
    this.reply(json);
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
    if (!this.windowOpen || !this.onGround) return { rc: MGMT_ERR_EACCESSDENIED };
    if (--this.closeWindowAfterPackets === 0) this.windowOpen = false;
    this.uploadOffsets.push(off);
    if (off === 0) {
      if (this.upload && sha && this.upload.sha.join() === sha.join()) return { off: this.upload.off };
      if (this.refuseUpload) return { err: { group: IMAGE_GROUP, rc: this.refuseUpload } };
      this.upload = { sha, len, off: 0 };
      this.slot = new Uint8Array(len);
      this.finished = false;
    }
    if (!this.upload || off !== this.upload.off) return { off: this.upload ? this.upload.off : 0 };
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

export function signedImage({ major, minor, revision, build }, size) {
  const bytes = new Uint8Array(size);
  for (let at = 0; at < size; at++) bytes[at] = (at * 31 + 7) & 0xff;
  const view = new DataView(bytes.buffer);
  view.setUint32(0, 0x96f3b83d, true);
  view.setUint8(20, major);
  view.setUint8(21, minor);
  view.setUint16(22, revision, true);
  view.setUint32(24, build, true);
  return bytes;
}
