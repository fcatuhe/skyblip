import { decode, encode } from './cbor.js';

export const OP = { read: 0, readResponse: 1, write: 2, writeResponse: 3 };
export const GROUP = { os: 0, image: 1 };
export const ID = { echo: 0, imageState: 0, imageUpload: 1, osParams: 6 };
export const RC = { accessDenied: 11 };

export const HEADER_BYTES = 8;
// INFO: fc 26sep26 header version 1 is SMP v2, which keeps the image group's own error codes
const SMP_V2 = 1;
const SEQ_MODULO = 256;
const REPLY_TIMEOUT_MS = 15_000;
// INFO: fc 26sep26 517, the ATT MTU Chrome asks for, less the 3-byte ATT header
export const LARGEST_ATT_PAYLOAD = 514;

export class SmpError extends Error {
  constructor(rc, group = null) {
    super(group === null ? `SMP rc ${rc}` : `SMP group ${group} rc ${rc}`);
    this.rc = rc;
    this.group = group;
  }
}

export class SmpWriteRejected extends Error {
  constructor(cause) {
    super(`the browser refused the write: ${cause.message}`, { cause });
  }
}

export class SmpTimeout extends Error {
  constructor() {
    super(`no SMP reply in ${REPLY_TIMEOUT_MS / 1000} s`);
  }
}

export function packet({ op, group, id, seq, body }) {
  const payload = encode(body);
  const bytes = new Uint8Array(HEADER_BYTES + payload.length);
  bytes[0] = (SMP_V2 << 3) | op;
  bytes[2] = payload.length >> 8;
  bytes[3] = payload.length & 0xff;
  bytes[4] = group >> 8;
  bytes[5] = group & 0xff;
  bytes[6] = seq;
  bytes[7] = id;
  bytes.set(payload, HEADER_BYTES);
  return bytes;
}

export function parseHeader(bytes) {
  return {
    op: bytes[0] & 0x07,
    length: (bytes[2] << 8) | bytes[3],
    group: (bytes[4] << 8) | bytes[5],
    seq: bytes[6],
    id: bytes[7],
  };
}

export function reassemblePackets(onPacket) {
  let held = new Uint8Array(0);
  return bytes => {
    const joined = new Uint8Array(held.length + bytes.length);
    joined.set(held);
    joined.set(bytes, held.length);
    held = joined;
    while (held.length >= HEADER_BYTES) {
      const end = HEADER_BYTES + parseHeader(held).length;
      if (held.length < end) break;
      onPacket(held.slice(0, end));
      held = held.slice(end);
    }
  };
}

function refusal(body) {
  if (body.err) return new SmpError(body.err.rc, body.err.group);
  if (body.rc) return new SmpError(body.rc);
  return null;
}

export class SmpClient {
  #write;
  #feed;
  #seq = 0;
  #waiting = null;
  #tail = Promise.resolve();
  #largestNotification = 0;

  constructor(write) {
    this.#write = write;
    this.#feed = reassemblePackets(bytes => this.#answer(bytes));
  }

  receive(bytes) {
    this.#largestNotification = Math.max(this.#largestNotification, bytes.length);
    this.#feed(bytes);
  }

  get largestNotification() {
    return this.#largestNotification;
  }

  request(op, group, id, body = {}) {
    const run = this.#tail.then(() => this.#exchange(op, group, id, body));
    this.#tail = run.then(settled, settled);
    return run;
  }

  close(error) {
    this.#largestNotification = 0;
    if (this.#waiting) this.#settle(null, error);
  }

  #exchange(op, group, id, body) {
    const seq = this.#seq;
    this.#seq = (this.#seq + 1) % SEQ_MODULO;
    const reply = new Promise((resolve, reject) => {
      this.#waiting = { seq, group, id, resolve, reject, timer: null };
    });
    const waiting = this.#waiting;
    this.#write(packet({ op, group, id, seq, body })).then(
      () => {
        if (this.#waiting === waiting) waiting.timer = setTimeout(() => this.#settle(null, new SmpTimeout()), REPLY_TIMEOUT_MS);
      },
      error => {
        if (this.#waiting === waiting) this.#settle(null, new SmpWriteRejected(error));
      },
    );
    return reply;
  }

  #answer(bytes) {
    const header = parseHeader(bytes);
    const waiting = this.#waiting;
    if (!waiting || header.seq !== waiting.seq || header.group !== waiting.group || header.id !== waiting.id) return;
    let body;
    try {
      body = decode(bytes.subarray(HEADER_BYTES));
    } catch (error) {
      this.#settle(null, error);
      return;
    }
    const refused = refusal(body);
    if (refused) this.#settle(null, refused);
    else this.#settle(body, null);
  }

  #settle(body, error) {
    const waiting = this.#waiting;
    if (!waiting) return;
    this.#waiting = null;
    clearTimeout(waiting.timer);
    if (error) waiting.reject(error);
    else waiting.resolve(body);
  }
}

function settled() {}

export async function packetBudget(client) {
  const { buf_size: size } = await client.request(OP.read, GROUP.os, ID.osParams);
  if (!Number.isInteger(size) || size <= HEADER_BYTES) throw new Error('the device reported no SMP buffer size');
  return size;
}

// INFO: fc 26sep26 smp_bt cuts a reply at MTU-3, so an echo longer than any payload measures the link
export async function probeWriteBytes(client, packetBytes) {
  const length = Math.min(LARGEST_ATT_PAYLOAD, packetBytes - HEADER_BYTES - encode({ d: '' }).length - 2);
  const d = 'x'.repeat(length);
  const { r } = await client.request(OP.write, GROUP.os, ID.echo, { d });
  if (r !== d) throw new Error('the device echoed something else');
  return Math.min(client.largestNotification, LARGEST_ATT_PAYLOAD);
}

export async function runningVersion(client) {
  const { images = [] } = await client.request(OP.read, GROUP.image, ID.imageState);
  const active = images.find(image => image.active) || images.find(image => image.slot === 0);
  return active ? active.version : null;
}

const BSTR_ONE_BYTE_HEAD_MAX = 23;
const BSTR_TWO_BYTE_HEAD_MAX = 0xff;

function largestData(room) {
  if (room - 3 > BSTR_TWO_BYTE_HEAD_MAX) return room - 3;
  if (room - 2 > BSTR_ONE_BYTE_HEAD_MAX) return Math.min(room - 2, BSTR_TWO_BYTE_HEAD_MAX);
  return Math.min(room - 1, BSTR_ONE_BYTE_HEAD_MAX);
}

function emptyBody(image, sha, off) {
  const empty = new Uint8Array(0);
  return off === 0 ? { len: image.length, off, sha, data: empty } : { off, data: empty };
}

export function dataRoom(image, sha, off, packetBytes) {
  return largestData(packetBytes - HEADER_BYTES - (encode(emptyBody(image, sha, off)).length - 1));
}

export function uploadBody(image, sha, off, packetBytes) {
  const length = dataRoom(image, sha, off, packetBytes);
  if (length <= 0) throw new Error(`an SMP packet of ${packetBytes} bytes has no room for image data`);
  const body = emptyBody(image, sha, off);
  body.data = image.subarray(off, Math.min(off + length, image.length));
  return body;
}

export async function upload(client, image, sha, packetBytes, onProgress) {
  let off = 0;
  let stalled = 0;
  while (off < image.length) {
    const reply = await client.request(OP.write, GROUP.image, ID.imageUpload, uploadBody(image, sha, off, packetBytes));
    if (!Number.isInteger(reply.off) || reply.off > image.length) throw new Error(`the device answered offset ${reply.off}`);
    stalled = reply.off === off ? stalled + 1 : 0;
    if (stalled > 1) throw new Error(`the device stopped at offset ${off}`);
    off = reply.off;
    onProgress(off, image.length);
  }
}
