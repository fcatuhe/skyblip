// INFO: fc 12nov26 a port of scripts/blip_records.py's diagnostics half, held to it byte for byte by test_diagnostics.mjs
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
const vouchedEnum8 = (at, bit, names) => (raw, flags) => (set(flags, bit) ? enum8(at, names)(raw) : undefined);
const flag = bit => (raw, flags) => set(flags, bit);
const bits = (at, shift, mask) => raw => (raw[at] >> shift) & mask;
const validOnly = (inner, bit) => (raw, flags) => (set(flags, bit) ? inner(raw, flags) : undefined);
const vouchedFlag = (recorded, bit) => (raw, flags) => (set(flags, recorded) ? set(flags, bit) : undefined);
const unlessUnknown = (inner, recorded, valid) => (raw, flags) => (set(flags, recorded) && !set(flags, valid) ? undefined : inner(raw, flags));
const scaled = (inner, factor) => (raw, flags) => inner(raw, flags) * factor;

const RESET = ['UNKNOWN', 'POWER ON', 'RESET PIN', 'BROWNOUT', 'SOFT RESET', 'WATCHDOG', 'CPU LOCKUP', 'CHARGER WAKE', 'BUTTON WAKE', 'DEBUGGER'];
const IMAGE_STATE = ['confirmed', 'probation', 'reverted', 'refused'];
const PROFILE = ['full', 'power_run', 'flight_run'];
const REJECT = ['NONE', 'NO SOLUTION', 'NO RMC', 'NO GGA', 'STALE', 'NO DATE', 'JUMP'];
const STAGE = ['silent', 'blind', 'solving', 'fixed'];
const VERDICT = ['transmitted', 'lost', 'held', 'unarmed', 'received', 'named', 'bad_crc', 'unframed', 'miskeyed', 'undecoded', 'unsupported', 'unattempted', 'declared'];
const SOURCE = ['adsl_direct', 'adsl_uplink', 'alptas', 'own', 'adsb'];
const BAND = ['M', 'O'];
const SLOT_STATE = ['uplink_rx_o_band', 'switch_o_band_to_m_band', 'slot0', 'hop', 'slot1', 'switch_m_band_to_o_band'];
const REFUSAL = ['none', 'over_budget', 'unarmed', 'unsettled', 'off_schedule'];
const FLIGHT_STATE = ['unknown', 'on_ground', 'airborne'];
const POWER_LEVEL = ['unknown', 'normal', 'critical', 'flat', 'low'];
const CHARGE = ['unknown', 'ok', 'too_cold', 'too_hot'];
const CONTACT = ['button', 'pad'];
const LINK_ACTION = ['up', 'down', 'claim_taken', 'claim_released', 'received', 'sent', 'dropped'];
const MTU_EXCHANGE_STARTER = ['none', 'central', 'unit', 'both'];
const ENDPOINT = ['config', 'nmea', 'log', 'peer'];
const PLACEMENT = ['idle', 'hold', 'place', 'forced'];
const WRITE_KIND = ['settings', 'flight_record'];
const CHANGE = ['retune', 'hop', 'to_o_band', 'to_m_band', 'wake'];
const SERVICE = ['config', 'ownship', 'power', 'radio', 'traffic', 'alarm', 'nmea', 'flight_log', 'capture', 'screen', 'board_poll', 'bravo'];

const TYPES = {
  1: ['boot', [
    ['capabilities', uint32(0)], ['firmware_build', uint32(4)], ['firmware_revision', uint16(8)],
    ['firmware_major', uint8(10)], ['firmware_minor', uint8(11)], ['reset', enum8(12, RESET)],
    ['image_state', enum8(13, IMAGE_STATE)], ['profile', vouchedEnum8(14, 2, PROFILE)]]],
  2: ['config', [
    ['address', uint32(0)], ['battery_offset_mv', int16(4)], ['frequency_trim_e1_ppm', int16(6)],
    ['aircraft_type', uint8(8)], ['address_mapping_table', uint8(9)], ['alarm_volume', uint8(10)],
    ['settings_version', uint8(11)], ['tx_power_dbm', int8(12)], ['pa_rated_dbm', int8(13)],
    ['alarm_enabled', flag(2)], ['metric', flag(3)], ['battery_trim_manual', flag(4)]]],
  3: ['gnss', [
    ['solution_phase_ms', uint16(0)], ['residual_m', uint16(2)], ['hdop_e2', uint16(4)], ['vdop_e2', uint16(6)],
    ['stage_s', uint16(8)], ['satellites', uint8(10)], ['satellites_in_view', uint8(11)], ['fix_mode', uint8(12)],
    ['reject', enum8(13, REJECT)], ['stage', enum8(14, STAGE)], ['fix_valid', flag(2)],
    ['residual_valid', flag(3)], ['pps_locked', flag(4)], ['geoid_measured', flag(5)],
    ['tx_settled', flag(6)], ['levels_fresh', flag(7)]]],
  4: ['pps', [
    ['interval_us', uint32(0)], ['error_us', int32(4)], ['samples', uint32(8)],
    ['holdover_events', uint16(12)], ['since_edge_ms', uint16(14)], ['locked', flag(2)],
    ['utc_valid', flag(3)]]],
  5: ['burst', [
    ['address', uint32(0)], ['tx_keyed_us', uint16(4)], ['tx_span_us', uint16(6)],
    ['verdict', enum8(8, VERDICT)], ['source', enum8(9, SOURCE)], ['band', enum8(10, BAND)],
    ['channel', uint8(11)], ['bytes', uint8(12)], ['rssi_dbm', int8(13)], ['key_offset_s', int8(14)],
    ['tx_stage_margin_us', scaled(int8(15), 10)], ['address_valid', flag(2)], ['rssi_valid', flag(3)], ['airborne', flag(4)],
    ['tx_span_valid', flag(5)], ['callsign', flag(6)], ['status', flag(7)]]],
  6: ['dwell', [
    ['frequency_hz', uint32(0)], ['start_ms', uint16(4)], ['end_ms', uint16(6)], ['phase_ms', uint16(8)],
    ['duty_permille', uint16(10)], ['state', enum8(12, SLOT_STATE)], ['band', enum8(13, BAND)],
    ['refusal', enum8(14, REFUSAL)], ['noise_dbm', int8(15)], ['tx_allowed', flag(2)],
    ['own_tx_dwell', flag(3)], ['listen_only', flag(4)], ['armed', flag(5)],
    ['burst_armed', flag(6)]]],
  7: ['flight', [
    ['speed_mm_s', int32(0)], ['climb_mm_s', int32(4)], ['altitude_msl_m', int16(8)],
    ['hdop_e2', uint16(10)], ['vdop_e2', uint16(12)], ['declared', enum8(14, FLIGHT_STATE)],
    ['confirmed', enum8(15, FLIGHT_STATE)], ['fix_valid', flag(2)], ['rolling', flag(3)],
    ['climb_valid', flag(4)], ['tx_settled', flag(5)]]],
  8: ['power', [
    ['cell_mv', uint16(0)], ['supply_warnings', uint16(2)], ['implausible', uint16(4)],
    ['charge_warnings', uint16(6)], ['die_dc', int16(8)], ['percent', uint8(10)],
    ['power_level', enum8(11, POWER_LEVEL)], ['charge', enum8(12, CHARGE)],
    ['trim_offset_mv', int16(13)], ['sample_offset_mv', int8(15)], ['charging', flag(2)], ['external_power', flag(3)],
    ['battery_valid', flag(4)], ['die_valid', flag(5)], ['caution', flag(6)],
    ['trim_learned', flag(7)]]],
  9: ['barometer', [
    ['pressure_mpa', uint32(0)], ['altitude_mm', int32(4)], ['climb_mm_s', int32(8)],
    ['temperature_dc', int16(12)], ['active', flag(2)], ['temperature_valid', flag(3)],
    ['climb_adopted', flag(4)]]],
  10: ['motion', [
    ['slip_mg', int16(0)], ['normal_mg', int16(2)], ['lateral_mg', int16(4)],
    ['longitudinal_mg', int16(6)], ['most_normal_mg', int16(8)], ['least_normal_mg', int16(10)],
    ['imu_error', uint8(12)], ['sensor_error', uint8(13)], ['slip_valid', flag(2)],
    ['gload_valid', flag(3)], ['fitted', flag(4)]]],
  11: ['contact', [
    ['at_ms', uint32(0)], ['held_ms', uint32(4)], ['contact', enum8(8, CONTACT)],
    ['gesture', uint8(9)], ['down', flag(2)]]],
  12: ['link', [
    ['link_session', uint16(0)], ['payload_bytes', uint16(2)], ['frame_bytes', uint16(4)],
    ['holder', uint16(6)], ['drops', uint16(8)], ['action', enum8(10, LINK_ACTION)],
    ['endpoint', enum8(11, ENDPOINT)],
    ['mtu_exchange_started_by', enum8(12, MTU_EXCHANGE_STARTER)], ['claim_held', flag(2)]]],
  13: ['traffic', [
    ['address', uint32(0)], ['distance_m', uint16(4)], ['bearing_deg', uint16(6)],
    ['relative_altitude_m', unlessUnknown(int16(8), 6, 7)],
    ['closing_mps', int16(10)], ['alarm', uint8(12)], ['source', enum8(13, SOURCE)],
    ['rssi_dbm', int8(14)], ['tracked', uint8(15)], ['assessed', flag(2)], ['dismissed', flag(3)],
    ['in_formation', flag(4)], ['position_valid', flag(5)],
    ['relative_altitude_valid', vouchedFlag(6, 7)]]],
  14: ['write', [
    ['waited_ms', uint32(0)], ['phase_ms', uint16(4)], ['requests', uint16(6)], ['writes', uint16(8)],
    ['forced', uint16(10)], ['placement', enum8(12, PLACEMENT)], ['kind', enum8(13, WRITE_KIND)],
    ['pending', flag(2)]]],
  15: ['screen', [
    ['since_page_shown_ms', uint32(0)], ['page', uint8(4)], ['mode', uint8(5)], ['prompt', uint8(6)],
    ['alarm', uint8(7)], ['render_us', uint32(8)], ['present_us', uint32(12)], ['backlight', flag(2)],
    ['powered', flag(3)], ['holding', flag(4)]]],
  16: ['gap', [
    ['dropped', uint32(0)], ['span_ms', uint32(4)], ['total', uint32(8)], ['capacity', uint16(12)]]],
  17: ['end', [
    ['records', uint32(0)], ['dropped', uint32(4)]]],
  18: ['duty', [
    ['panel_partial_refreshes', uint16(0)], ['panel_full_refreshes', uint16(2)],
    ['backlight_ms', uint16(4)], ['rx_armed_ms', uint16(6)], ['tx_keyed_ms', uint16(8)],
    ['ble_connected_ms', uint16(10)], ['annunciator_ms', uint16(12)]]],
  19: ['switch', [
    ['to_hz', uint32(0)], ['margin_us', int32(4)], ['took_us', uint16(8)], ['gap_us', uint16(10)],
    ['change', enum8(12, CHANGE)], ['armed_ahead', flag(2)], ['late', flag(3)]]],
  20: ['loop', [
    ['passes', uint16(0)], ['busy_ms', uint16(2)], ['worst_pass_us', uint32(4)],
    ['worst_pass_phase_ms', uint16(8)], ['worst_gap_ms', uint16(10)], ['worst_gap_phase_ms', uint16(12)],
    ['worst_service', enum8(14, SERVICE)], ['worst_tick_ms', uint8(15)]]],
  21: ['adsb', [
    ['address', uint24(0)], ['adsb_version', validOnly(bits(3, 0, 0x07), 2)],
    ['gva', validOnly(bits(3, 3, 0x03), 3)], ['pressure_altitude_ft', validOnly(int32(4), 4)],
    ['height_difference_ft', validOnly(int16(8), 5)],
    ['height_difference_age_ms', validOnly(int16(10), 5)],
    ['gnss_relative_altitude_m', validOnly(int16(12), 6)],
    ['pressure_relative_altitude_m', validOnly(int16(14), 7)], ['adsb_version_valid', flag(2)],
    ['gva_valid', flag(3)], ['pressure_altitude_valid', flag(4)], ['height_difference_valid', flag(5)],
    ['gnss_relative_altitude_valid', flag(6)], ['pressure_relative_altitude_valid', flag(7)]]],
};

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
    closed: Boolean(session.closed), truncated: Boolean(session.truncated),
  }];
  const kept = records.map(record => ({ log: LOG, session: session.session, ...record }));
  const torn = session.closed === false && kept.length > 0 ? kept.pop() : null;
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
