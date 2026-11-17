// INFO: fc 17nov26 the serial API's framing and the few protobuf fields we read, meshtastic/protobufs mesh.proto and admin.proto
const START1 = 0x94;
const START2 = 0xc3;
const MAX_FRAME_BYTES = 512;
const WAKE_BYTES = 32;
// INFO: fc 17nov26 PhoneAPI's SPECIAL_NONCE_ONLY_CONFIG: my_info, our own node and metadata, without the node database
const ONLY_CONFIG = 69420;
const ADMIN_APP = 6;
const ENTER_DFU_MODE_REQUEST = 21;

const FIELD = {
  fromRadio: { myInfo: 3, nodeInfo: 4, configCompleteId: 7, metadata: 13 },
  toRadio: { packet: 1, wantConfigId: 3 },
  myInfo: { myNodeNum: 1 },
  nodeInfo: { num: 1, user: 2 },
  user: { hwModel: 5 },
  metadata: { firmwareVersion: 1, hwModel: 9 },
  meshPacket: { to: 2, decoded: 4, id: 6 },
  data: { portnum: 1, payload: 2 },
};

// INFO: fc 17nov26 mesh.proto's HardwareModel numbers of the boards a skyBlip image is built for
export const PRODUCT_OF_MODEL = { 7: 'go', 33: 'go', 128: 'bravo' };

export const IDENTIFY_WITHIN_MS = 4_000;

function varint(value) {
  const bytes = [];
  let rest = value >>> 0;
  while (rest > 0x7f) {
    bytes.push((rest & 0x7f) | 0x80);
    rest >>>= 7;
  }
  bytes.push(rest);
  return bytes;
}

function tag(field, wireType) {
  return varint((field << 3) | wireType);
}

function fixed32(value) {
  return [value & 0xff, (value >>> 8) & 0xff, (value >>> 16) & 0xff, (value >>> 24) & 0xff];
}

function lengthDelimited(field, bytes) {
  return [...tag(field, 2), ...varint(bytes.length), ...bytes];
}

export function frame(payload) {
  return Uint8Array.of(START1, START2, payload.length >> 8, payload.length & 0xff, ...payload);
}

export function wantConfig(nonce = ONLY_CONFIG) {
  return frame([...tag(FIELD.toRadio.wantConfigId, 0), ...varint(nonce)]);
}

// INFO: fc 17nov26 AdminModule accepts a packet from the local client to our own node without a session key
export function enterDfuMode(nodeNum, id = 1) {
  const admin = [...tag(ENTER_DFU_MODE_REQUEST, 0), 1];
  const data = [...tag(FIELD.data.portnum, 0), ADMIN_APP, ...lengthDelimited(FIELD.data.payload, admin)];
  const packet = [
    ...tag(FIELD.meshPacket.to, 5), ...fixed32(nodeNum),
    ...lengthDelimited(FIELD.meshPacket.decoded, data),
    ...tag(FIELD.meshPacket.id, 5), ...fixed32(id),
  ];
  return frame(lengthDelimited(FIELD.toRadio.packet, packet));
}

export function wakeBytes() {
  return new Uint8Array(WAKE_BYTES).fill(START2);
}

export function fieldsOf(bytes) {
  const fields = [];
  let at = 0;
  const readVarint = () => {
    let value = 0;
    let shift = 0;
    for (;;) {
      if (at >= bytes.length) throw new RangeError('a varint runs past the message');
      const byte = bytes[at++];
      value += (byte & 0x7f) * 2 ** shift;
      if (byte < 0x80) return value;
      shift += 7;
    }
  };
  while (at < bytes.length) {
    const key = readVarint();
    const number = Math.floor(key / 8);
    const wireType = key % 8;
    if (wireType === 0) fields.push([number, readVarint()]);
    else if (wireType === 1) at += 8;
    else if (wireType === 2) {
      const length = readVarint();
      if (at + length > bytes.length) throw new RangeError('a field runs past the message');
      fields.push([number, bytes.subarray(at, at + length)]);
      at += length;
    } else if (wireType === 5) {
      if (at + 4 > bytes.length) throw new RangeError('a fixed32 runs past the message');
      fields.push([number, (bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | (bytes[at + 3] << 24)) >>> 0]);
      at += 4;
    } else throw new RangeError(`wire type ${wireType} is not one mesh.proto uses`);
  }
  return fields;
}

function field(fields, number) {
  return fields.find(([each]) => each === number)?.[1];
}

export function readFromRadio(payload) {
  const fields = fieldsOf(payload);
  const myInfo = field(fields, FIELD.fromRadio.myInfo);
  if (myInfo) return { myNodeNum: field(fieldsOf(myInfo), FIELD.myInfo.myNodeNum) ?? null };
  const metadata = field(fields, FIELD.fromRadio.metadata);
  if (metadata) {
    const read = fieldsOf(metadata);
    const version = field(read, FIELD.metadata.firmwareVersion);
    return { hwModel: field(read, FIELD.metadata.hwModel) ?? null, firmwareVersion: version ? new TextDecoder().decode(version) : null };
  }
  const nodeInfo = field(fields, FIELD.fromRadio.nodeInfo);
  if (nodeInfo) {
    const read = fieldsOf(nodeInfo);
    const user = field(read, FIELD.nodeInfo.user);
    return { node: field(read, FIELD.nodeInfo.num) ?? null, nodeModel: user ? field(fieldsOf(user), FIELD.user.hwModel) ?? null : null };
  }
  if (field(fields, FIELD.fromRadio.configCompleteId) !== undefined) return { configComplete: true };
  return null;
}

// INFO: fc 17nov26 the node logs text on the same port until it hears the API, so bytes outside a frame are skipped
export class FrameReader {
  #held = [];

  constructor(onFrame) {
    this.onFrame = onFrame;
  }

  feed(bytes) {
    this.#held.push(...bytes);
    for (;;) {
      const start = this.#held.findIndex((byte, index) => byte === START1 && this.#held[index + 1] === START2);
      if (start < 0) {
        this.#held = this.#held.at(-1) === START1 ? [START1] : [];
        return;
      }
      this.#held = this.#held.slice(start);
      if (this.#held.length < 4) return;
      const length = (this.#held[2] << 8) | this.#held[3];
      if (length > MAX_FRAME_BYTES) {
        this.#held = this.#held.slice(1);
        continue;
      }
      if (this.#held.length < 4 + length) return;
      const payload = Uint8Array.from(this.#held.slice(4, 4 + length));
      this.#held = this.#held.slice(4 + length);
      this.onFrame(payload);
    }
  }
}

export function identityOf(heard) {
  const hwModel = heard.hwModel ?? heard.nodeModel ?? null;
  const nodeNum = heard.myNodeNum ?? null;
  return {
    nodeNum,
    hwModel,
    product: PRODUCT_OF_MODEL[hwModel] ?? null,
    // INFO: fc 17nov26 Meshtastic numbers a nRF52 node by the low four bytes of FICR DEVICEADDR, skyBlip tags it by the low two
    tag: nodeNum === null ? null : nodeNum & 0xffff,
    firmwareVersion: heard.firmwareVersion ?? null,
  };
}

function answeredAll(heard) {
  return heard.configComplete === true || (heard.myNodeNum !== undefined && heard.hwModel !== undefined);
}

export class MeshtasticLink {
  #reader;
  #writer;
  #heard = {};
  #listeners = new Set();
  #reading;

  constructor({ readable, writable }) {
    this.#reader = readable.getReader();
    this.#writer = writable.getWriter();
    const frames = new FrameReader(payload => this.#take(payload));
    this.#reading = this.#read(frames);
  }

  async identify({ withinMs = IDENTIFY_WITHIN_MS } = {}) {
    let listener;
    const answered = new Promise(resolve => {
      const timer = setTimeout(resolve, withinMs);
      listener = () => {
        if (!answeredAll(this.#heard)) return;
        clearTimeout(timer);
        resolve();
      };
      this.#listeners.add(listener);
    });
    await this.#writer.write(wakeBytes());
    await this.#writer.write(wantConfig());
    await answered;
    this.#listeners.delete(listener);
    return this.#heard.myNodeNum === undefined ? null : identityOf(this.#heard);
  }

  restartIntoDrive(nodeNum) {
    return this.#writer.write(enterDfuMode(nodeNum, (Math.random() * 0xffffffff) >>> 0));
  }

  async close() {
    await this.#reader.cancel().catch(error => console.warn('The serial port closed while it was read:', error.message));
    await this.#reading;
    this.#reader.releaseLock();
    this.#writer.releaseLock();
  }

  #take(payload) {
    let read;
    try {
      read = readFromRadio(payload);
    } catch (error) {
      console.warn('A Meshtastic frame did not decode:', error.message);
      return;
    }
    if (!read) return;
    Object.assign(this.#heard, read);
    for (const listener of this.#listeners) listener();
  }

  async #read(frames) {
    for (;;) {
      let chunk;
      try {
        chunk = await this.#reader.read();
      } catch (error) {
        console.warn('The serial port stopped answering:', error.message);
        return;
      }
      if (chunk.done) return;
      frames.feed(chunk.value);
    }
  }
}
