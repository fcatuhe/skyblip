const MAJOR = { uint: 0, nint: 1, bytes: 2, text: 3, array: 4, map: 5, tag: 6, simple: 7 };
const INDEFINITE = 31;
const BREAK = 0xff;
const SIMPLE = { false: 20, true: 21, null: 22, undefined: 23, half: 25, single: 26, double: 27 };
const TWO_TO_32 = 2 ** 32;

const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });

export function encode(value) {
  const out = [];
  put(out, value);
  return Uint8Array.from(out);
}

function head(out, major, length) {
  const type = major << 5;
  if (length < 24) {
    out.push(type | length);
  } else if (length < 0x100) {
    out.push(type | 24, length);
  } else if (length < 0x10000) {
    out.push(type | 25, length >> 8, length & 0xff);
  } else if (length < TWO_TO_32) {
    out.push(type | 26, ...bigEndian(length, 4));
  } else {
    out.push(type | 27, ...bigEndian(Math.floor(length / TWO_TO_32), 4), ...bigEndian(length % TWO_TO_32, 4));
  }
}

function bigEndian(value, width) {
  const bytes = [];
  for (let shift = (width - 1) * 8; shift >= 0; shift -= 8) bytes.push(Math.floor(value / 2 ** shift) & 0xff);
  return bytes;
}

function put(out, value) {
  if (value === null) {
    out.push(0xe0 | SIMPLE.null);
  } else if (typeof value === 'boolean') {
    out.push(0xe0 | (value ? SIMPLE.true : SIMPLE.false));
  } else if (typeof value === 'number') {
    if (!Number.isSafeInteger(value)) throw new TypeError(`cbor: ${value} is not a safe integer`);
    if (value >= 0) head(out, MAJOR.uint, value);
    else head(out, MAJOR.nint, -1 - value);
  } else if (typeof value === 'string') {
    const bytes = encoder.encode(value);
    head(out, MAJOR.text, bytes.length);
    out.push(...bytes);
  } else if (value instanceof Uint8Array) {
    head(out, MAJOR.bytes, value.length);
    for (const byte of value) out.push(byte);
  } else if (Array.isArray(value)) {
    head(out, MAJOR.array, value.length);
    for (const item of value) put(out, item);
  } else if (typeof value === 'object') {
    const entries = Object.entries(value).filter(([, item]) => item !== undefined);
    head(out, MAJOR.map, entries.length);
    for (const [key, item] of entries) {
      put(out, key);
      put(out, item);
    }
  } else {
    throw new TypeError(`cbor: cannot encode ${typeof value}`);
  }
}

export function decode(bytes) {
  const reader = new Reader(bytes);
  const value = reader.item();
  if (reader.at !== bytes.length) throw new RangeError(`cbor: ${bytes.length - reader.at} bytes after the item`);
  return value;
}

class Reader {
  constructor(bytes) {
    this.bytes = bytes;
    this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    this.at = 0;
  }

  byte() {
    if (this.at >= this.bytes.length) throw new RangeError('cbor: truncated');
    return this.bytes[this.at++];
  }

  take(length) {
    if (this.at + length > this.bytes.length) throw new RangeError('cbor: truncated');
    const slice = this.bytes.slice(this.at, this.at + length);
    this.at += length;
    return slice;
  }

  number(width) {
    let value = 0;
    for (const byte of this.take(width)) value = value * 0x100 + byte;
    if (!Number.isSafeInteger(value)) throw new RangeError('cbor: integer beyond 2^53');
    return value;
  }

  argument(info) {
    if (info < 24) return info;
    if (info === 24) return this.number(1);
    if (info === 25) return this.number(2);
    if (info === 26) return this.number(4);
    if (info === 27) return this.number(8);
    throw new RangeError(`cbor: reserved additional information ${info}`);
  }

  atBreak() {
    if (this.at >= this.bytes.length) throw new RangeError('cbor: truncated');
    if (this.bytes[this.at] !== BREAK) return false;
    this.at++;
    return true;
  }

  item() {
    const initial = this.byte();
    const major = initial >> 5;
    const info = initial & 0x1f;
    if (major === MAJOR.simple) return this.simple(info);
    if (info === INDEFINITE) return this.indefinite(major);
    const argument = this.argument(info);
    switch (major) {
      case MAJOR.uint: return argument;
      case MAJOR.nint: return -1 - argument;
      case MAJOR.bytes: return this.take(argument);
      case MAJOR.text: return decoder.decode(this.take(argument));
      case MAJOR.array: return Array.from({ length: argument }, () => this.item());
      case MAJOR.map: {
        let remaining = argument;
        return this.entries(() => remaining-- > 0);
      }
      default: return this.item();
    }
  }

  indefinite(major) {
    switch (major) {
      case MAJOR.bytes: return this.joined(MAJOR.bytes);
      case MAJOR.text: return decoder.decode(this.joined(MAJOR.text));
      case MAJOR.array: {
        const items = [];
        while (!this.atBreak()) items.push(this.item());
        return items;
      }
      case MAJOR.map: return this.entries(() => !this.atBreak());
      default: throw new RangeError(`cbor: major type ${major} cannot be indefinite`);
    }
  }

  joined(major) {
    const parts = [];
    while (!this.atBreak()) {
      const initial = this.byte();
      if (initial >> 5 !== major || (initial & 0x1f) === INDEFINITE) throw new RangeError('cbor: bad string chunk');
      parts.push(this.take(this.argument(initial & 0x1f)));
    }
    const joined = new Uint8Array(parts.reduce((sum, part) => sum + part.length, 0));
    let at = 0;
    for (const part of parts) {
      joined.set(part, at);
      at += part.length;
    }
    return joined;
  }

  entries(more) {
    const map = {};
    while (more()) {
      const key = String(this.item());
      Object.defineProperty(map, key, { value: this.item(), enumerable: true, writable: true, configurable: true });
    }
    return map;
  }

  simple(info) {
    switch (info) {
      case SIMPLE.false: return false;
      case SIMPLE.true: return true;
      case SIMPLE.null: return null;
      case SIMPLE.undefined: return undefined;
      case SIMPLE.half: return half(this.number(2));
      case SIMPLE.single: return this.float(4);
      case SIMPLE.double: return this.float(8);
      default: throw new RangeError(`cbor: unsupported simple value ${info}`);
    }
  }

  float(width) {
    const value = width === 4 ? this.view.getFloat32(this.at) : this.view.getFloat64(this.at);
    this.take(width);
    return value;
  }
}

function half(bits) {
  const exponent = (bits >> 10) & 0x1f;
  const mantissa = bits & 0x3ff;
  const sign = bits & 0x8000 ? -1 : 1;
  if (exponent === 0) return sign * 2 ** -14 * (mantissa / 1024);
  if (exponent === 31) return mantissa ? NaN : sign * Infinity;
  return sign * 2 ** (exponent - 15) * (1 + mantissa / 1024);
}
