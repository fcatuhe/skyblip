// INFO: fc 22nov26 the record tables are generated in diagnostics_layout.js, the NDJSON ports blip_ndjson.py, and test_diagnostics.mjs holds both to Python
import { DIAGNOSTICS_LAYOUT } from '#skyblip/diagnostics_layout';

export const RECORD_BYTES = 24;
const PAYLOAD_OFFSET = 8;
const LOG = 'diagnostics';
const TORN_TAIL_REASON = 'the last record of a session the device did not close may be a torn write';

export class RecordError extends Error {}

const view = raw => new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
const set = (flags, bit) => Boolean(flags & (1 << bit));

const uint8 = at => raw => raw[at];
const int8 = at => raw => view(raw).getInt8(at);
const uint16 = at => raw => view(raw).getUint16(at, true);
const int16 = at => raw => view(raw).getInt16(at, true);
const uint24 = at => raw => raw[at] | (raw[at + 1] << 8) | (raw[at + 2] << 16);
const uint32 = at => raw => view(raw).getUint32(at, true);
const int32 = at => raw => view(raw).getInt32(at, true);
const enum8 = (at, names) => raw => names[raw[at]] ?? raw[at];
const flag = bit => (raw, flags) => set(flags, bit);
const bits = (at, shift, mask) => raw => (raw[at] >> shift) & mask;
const validOnly = (inner, bit) => (raw, flags) => (set(flags, bit) ? inner(raw, flags) : undefined);
const unlessUnknown = (inner, recorded, valid) => (raw, flags) => (set(flags, recorded) && !set(flags, valid) ? undefined : inner(raw, flags));
const scaled = (inner, factor) => (raw, flags) => inner(raw, flags) * factor;
const READERS = { u8: uint8, i8: int8, u16: uint16, i16: int16, u24: uint24, u32: uint32, i32: int32 };

function reader(field) {
  let read;
  if (field.kind === 'flag') read = flag(field.bit);
  else if (field.kind === 'enum8') read = enum8(field.at, DIAGNOSTICS_LAYOUT.enums[field.enum]);
  else if (field.kind === 'bits') read = bits(field.at, field.shift, field.mask);
  else read = READERS[field.kind](field.at);
  if (field.scale !== undefined) read = scaled(read, field.scale);
  if (field.vouch_bit !== undefined) return unlessUnknown(read, field.vouch_bit, field.valid_bit);
  if (field.valid_bit !== undefined) return validOnly(read, field.valid_bit);
  return read;
}

const TYPES = Object.fromEntries(DIAGNOSTICS_LAYOUT.types.map(([code, name, fields]) => [code, [name, fields.map(field => [field.name, reader(field)])]]));

export function decodeDiagnosticsRecord(raw) {
  if (raw.length !== RECORD_BYTES) throw new RecordError(`short slot of ${raw.length} bytes`);
  if (raw.every(byte => byte === 0xff)) throw new RecordError('erased');
  if (!TYPES[raw[0]]) throw new RecordError(`unknown type ${raw[0]}`);
  const [type, fields] = TYPES[raw[0]];
  const flags = raw[1];
  const payload = raw.subarray(PAYLOAD_OFFSET);
  const decoded = { type, at_s: uint32(4)(raw), into_ms: uint16(2)(raw), phase_valid: set(flags, 0), utc_dated: set(flags, 1) };
  for (const [field, read] of fields) {
    const value = read(payload, flags);
    if (value !== undefined) decoded[field] = value;
  }
  return decoded;
}

export function decodeChunk(raw, firstIndex) {
  if (raw.length % RECORD_BYTES) throw new RecordError(`chunk of ${raw.length} bytes is not whole records`);
  const out = [];
  for (let slot = 0; slot < raw.length / RECORD_BYTES; slot++) {
    let decoded;
    try {
      decoded = decodeDiagnosticsRecord(raw.subarray(slot * RECORD_BYTES, (slot + 1) * RECORD_BYTES));
    } catch (error) {
      if (!(error instanceof RecordError)) throw error;
      decoded = { type: 'unreadable', error: error.message };
    }
    out.push({ index: firstIndex + slot, ...decoded });
  }
  return out;
}

// INFO: fc 12nov26 a diagnostics record carries no CRC, so the last of a session cut by a power loss may be torn
export function sessionLines(session, records) {
  const lines = [{
    type: 'session', log: LOG, session: session.session, records: session.records,
    closed: Boolean(session.closed), truncated: Boolean(session.truncated), ...(session.recording && { recording: true }),
  }];
  const kept = records.map(record => ({ log: LOG, session: session.session, ...record }));
  const torn = session.closed === false && !session.recording && kept.length > 0 ? kept.pop() : null;
  lines.push(...kept);
  if (torn) lines.push({ type: 'torn', log: LOG, session: session.session, index: torn.index, reason: TORN_TAIL_REASON });
  return lines;
}

export function isNote(line) {
  return line.type === 'session' || line.type === 'torn';
}

// INFO: fc 12nov26 Python's json.dumps spacing, so a download diffs clean against a file blip.py fetched
export function ndjson(lines) {
  return lines.map(line => `{${Object.entries(line).map(([key, value]) => `${JSON.stringify(key)}: ${JSON.stringify(value)}`).join(', ')}}\n`).join('');
}

export function typeCounts(lines) {
  const counts = {};
  for (const line of lines) if (!isNote(line)) counts[line.type] = (counts[line.type] ?? 0) + 1;
  return Object.fromEntries(Object.entries(counts).sort(([a], [b]) => a.localeCompare(b)));
}

export function aircraftOf(lines) {
  const aircraft = new Map();
  for (const line of lines) {
    if (line.type !== 'adsb') continue;
    const seen = aircraft.get(line.address) ?? { address: line.address, records: 0, latest: line };
    seen.records++;
    if (line.index >= seen.latest.index) seen.latest = line;
    aircraft.set(line.address, seen);
  }
  return [...aircraft.values()];
}
