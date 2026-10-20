import { decode, encode } from '#skyblip/cbor';
import { LINK_REGISTRY } from '#skyblip/link_registry';

const COMMAND_KEY = 0;

export class FrameError extends Error {}

const FRAMES = new Map(LINK_REGISTRY.frames.map(frame => [frame.command, tableOf(frame)]));
const BY_CODE = new Map([...FRAMES.values()].map(frame => [frame.code, frame]));

const ARGUMENT_CEILING = 2 ** 32 - 1;
const isUnsigned = value => Number.isInteger(value) && value >= 0 && value <= ARGUMENT_CEILING;
const ACCEPTS = {
  unsigned: isUnsigned,
  integer: value => Number.isInteger(value) && value >= -ARGUMENT_CEILING - 1 && value <= ARGUMENT_CEILING,
  boolean: value => typeof value === 'boolean',
  text: value => typeof value === 'string' && value.isWellFormed(),
  bytes: value => value instanceof Uint8Array,
  unsigned_array: value => Array.isArray(value) && value.every(isUnsigned),
  enum: isUnsigned,
};

function tableOf({ command, code, keys }) {
  return {
    command,
    code,
    byName: new Map(keys.map(key => [key.name, key])),
    byNumber: new Map(keys.map(key => [key.number, key])),
  };
}

export function encodeFrame(fields) {
  if (fields === null || typeof fields !== 'object' || Array.isArray(fields)) throw new FrameError(`a frame is an object of names, not ${printable(fields)}`);
  const frame = frameOf(fields.command);
  const entries = Object.entries(fields).filter(([, value]) => value !== undefined).map(([name, value]) => {
    const key = frame.byName.get(name);
    if (!key) throw new FrameError(`${frame.command} has no key ${name}`);
    return [key.number, wireValue(key, value)];
  });
  return encode(new Map(entries.sort(([a], [b]) => a - b)));
}

function frameOf(command) {
  const frame = typeof command === 'string' ? FRAMES.get(command) : isUnsigned(command) ? BY_CODE.get(command) : undefined;
  if (!frame) throw new FrameError(`no frame is commanded ${JSON.stringify(command)}`);
  return frame;
}

function wireValue(key, value) {
  let wire = value;
  if (typeof value === 'string' && key.type === 'enum') wire = enumCode(key, value);
  if (typeof value === 'string' && key.type === 'bytes') wire = bytesOfHex(key, value);
  if (!ACCEPTS[key.type](wire) || (key.bytes !== undefined && wire.length !== key.bytes)) {
    const width = key.bytes === undefined ? '' : ` of ${key.bytes}`;
    throw new FrameError(`${key.name} takes ${key.type}${width}, not ${printable(value)}`);
  }
  return wire;
}

function enumCode(key, name) {
  const value = LINK_REGISTRY.enums[key.enum].values.find(value => value.name === name);
  if (!value) throw new FrameError(`${key.name} has no value named ${name}`);
  return value.code;
}

function bytesOfHex(key, text) {
  if (!/^(?:[0-9a-f]{2})*$/i.test(text)) throw new FrameError(`${key.name} takes bytes, as hex, not ${text}`);
  return Uint8Array.from(text.match(/../g) ?? [], pair => parseInt(pair, 16));
}

export function decodeFrame(bytes) {
  let entries;
  try {
    entries = decode(bytes, { deterministic: true });
  } catch (error) {
    throw new FrameError(error.message);
  }
  if (!isUnsigned(entries.get(COMMAND_KEY))) throw new FrameError(`key 0 names the frame, and ${printable(entries.get(COMMAND_KEY))} does not`);
  const frame = BY_CODE.get(entries.get(COMMAND_KEY));
  const fields = {};
  for (const [number, value] of entries) {
    const key = frame?.byNumber.get(number);
    if (key) fields[key.name] = key.type === 'enum' ? enumName(key, value) : value;
    else fields[number === COMMAND_KEY ? 'command' : String(number)] = value;
  }
  return fields;
}

function enumName(key, code) {
  return LINK_REGISTRY.enums[key.enum].values.find(value => value.code === code)?.name ?? code;
}

export function hex(bytes) {
  return Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');
}

export function printable(fields) {
  return JSON.stringify(fields, (name, value) => (value instanceof Uint8Array ? hex(value) : value));
}
