import { decode, encode } from './cbor.js';
import { UUID } from './ble.js';

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const SMP_HEADER_BYTES = 8;
const ATT_HEADER_BYTES = 3;
const MGMT_ERR_EMSGSIZE = 7;
const MGMT_ERR_ENOTSUP = 8;
const MGMT_ERR_EACCESSDENIED = 11;
const IMAGE_GROUP = 1;
export const IMG_MGMT_ERR_CURRENT_VERSION_IS_NEWER = 27;

const tick = () => new Promise(resolve => setImmediate(resolve));

class Characteristic {
  constructor(link, onWrite) {
    this.link = link;
    this.onWrite = onWrite;
    this.listeners = [];
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
    return this.link.write(this, bytes);
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
  } = {}) {
    Object.assign(this, { running, image, from, to, settings, onGround, swapPowered, bufSize, mtu, claimedBy });
    this.hasSmp = smp;
    this.connected = false;
    this.pending = null;
    this.windowOpen = false;
    this.slot = null;
    this.finished = false;
    this.upload = null;
    this.commands = [];
    this.uploadOffsets = [];
    this.longestWrite = 0;
    this.longestPacket = 0;
    this.overlaps = 0;
    this.writing = false;
    this.refuseApply = null;
    this.refuseUpload = null;
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

  async write(characteristic, bytes) {
    if (!this.connected) throw new Error('GATT Server is disconnected.');
    if (this.writing) this.overlaps++;
    this.writing = true;
    this.longestWrite = Math.max(this.longestWrite, bytes.length);
    await tick();
    this.writing = false;
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
    const { cmd } = JSON.parse(decoder.decode(bytes));
    this.commands.push(cmd);
    if (this.claimedBy !== null) return this.reply({ ack: false, reason: 'claimed', by: this.claimedBy });
    if (cmd === 'update') return this.sendUpdate();
    if (cmd === 'status') return this.sendStatus();
    if (!['dfu', 'apply', 'recovery'].includes(cmd)) return this.ack(false, 'unknown_cmd');
    if (!this.onGround) return this.ack(false, 'in_flight');
    if (cmd !== 'recovery' && !this.swapPowered) return this.ack(false, 'low_power');
    if (cmd === 'apply' && this.stagingRefusal()) return this.ack(false, this.stagingRefusal());
    this.pending = cmd;
    this.reply({ ack: false, pending: true, reason: `confirm_${cmd}` });
  }

  stagingRefusal() {
    if (this.refuseApply) return this.refuseApply;
    if (!this.slot) return 'nothing_staged';
    if (!this.finished) return 'upload_unfinished';
    return null;
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
    if (task === 'dfu') {
      this.windowOpen = true;
      this.finished = false;
      return this.ack(true, 'dfu');
    }
    if (task === 'apply' && this.stagingRefusal()) return this.ack(false, this.stagingRefusal());
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
    else if (group === 0 && id === 6) answer = { buf_size: this.bufSize, buf_count: 4 };
    else if (group === 1 && id === 0) answer = { images: [{ slot: 0, version: this.running, active: true, confirmed: true }] };
    else if (upload) answer = this.imageUpload(body);
    else answer = { rc: MGMT_ERR_ENOTSUP };
    this.sendSmp(request, answer);
    if (upload && --this.dropAfterPackets === 0) setImmediate(() => this.drop());
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
