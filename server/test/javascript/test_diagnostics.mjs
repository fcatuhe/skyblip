import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

import { aircraftOf, decodeChunk, decodeDiagnosticsRecord, ndjson, sessionLines, typeCounts } from '#skyblip/diagnostics';

const bytes = text => Uint8Array.from(text.match(/../g) ?? [], pair => parseInt(pair, 16));
const SCRIPTS = fileURLToPath(new URL('../../../scripts', import.meta.url));
const BASE_UTC_S = 1_785_628_800;

const record = (type, flags, payload = []) => {
  const raw = new Uint8Array(24);
  raw[0] = type;
  raw[1] = flags;
  raw.set(payload, 8);
  return raw;
};

const join = records => {
  const out = new Uint8Array(records.length * 24);
  records.forEach((raw, at) => out.set(raw, at * 24));
  return out;
};

// The record firmware/test/products/test_adsb_capture.cpp pins, as scripts/test_blip_altitude.py reads it.
test('a host capture decodes to the target the rig heard', () => {
  const decoded = decodeDiagnosticsRecord(bytes('15ffc80083886e6a5e4d3c12b80b0000edfeb004e5017a02'));
  assert.deepEqual(decoded, {
    type: 'adsb', at_s: BASE_UTC_S + 3, into_ms: 200, phase_valid: true, utc_dated: true, address: 0x3c4d5e,
    adsb_version: 2, gva: 2, pressure_altitude_ft: 3000, height_difference_ft: -275, height_difference_age_ms: 1200,
    gnss_relative_altitude_m: 485, pressure_relative_altitude_m: 634, adsb_version_valid: true, gva_valid: true,
    pressure_altitude_valid: true, height_difference_valid: true, gnss_relative_altitude_valid: true,
    pressure_relative_altitude_valid: true,
  });
});

test('a version never heard is absent, and version zero is zero', () => {
  const unheard = decodeDiagnosticsRecord(record(21, 0, [0x5e, 0x4d, 0x3c, 0x00]));
  assert.equal('adsb_version' in unheard, false);
  assert.equal(unheard.adsb_version_valid, false);
  const heard = decodeDiagnosticsRecord(record(21, 0b0000_0100, [0x5e, 0x4d, 0x3c, 0x00]));
  assert.equal(heard.adsb_version, 0);
  assert.equal(heard.adsb_version_valid, true);
});

test('a capture target with no altitude decodes as unknown', () => {
  const decoded = decodeDiagnosticsRecord(bytes('0d66000040f9a16a5e4d3c00c8050c00000000000104b903'));
  assert.equal(decoded.type, 'traffic');
  assert.equal(decoded.source, 'adsb');
  assert.equal(decoded.relative_altitude_valid, false);
  assert.equal('relative_altitude_m' in decoded, false);
});

test('a slot that is no record decodes as unreadable, and says why', () => {
  const decoded = decodeChunk(join([new Uint8Array(24).fill(0xff), record(99, 0), record(17, 0, [2])]), 40);
  assert.deepEqual(decoded.map(({ index, type, error }) => [index, type, error]), [
    [40, 'unreadable', 'erased'], [41, 'unreadable', 'unknown type 99'], [42, 'end', undefined],
  ]);
});

// Two tools read one capture, so the console's download must be the file blip.py writes, byte for byte.
test('every record type decodes to the NDJSON blip.py writes for it', () => {
  const records = [];
  for (let type = 1; type <= 24; type++) {
    for (const flags of [0x00, 0x55, 0xaa, 0xff]) {
      records.push(Uint8Array.from({ length: 24 }, (_, at) => (at === 0 ? type : at === 1 ? flags : (type * 37 + flags + at * 59) & 0xff)));
    }
  }
  records.push(new Uint8Array(24).fill(0xff));
  for (const closed of [false, true]) {
    const session = { session: BASE_UTC_S, records: records.length, closed, truncated: !closed };
    assert.equal(ndjson(sessionLines(session, decodeChunk(join(records), 0))), blipWrites(session, records));
  }
});

function blipWrites(session, records) {
  return execFileSync('python3', ['-c', `
import json, sys
sys.path.insert(0, sys.argv[1])
import blip_records as records
session = json.loads(sys.argv[2])
raw = bytes.fromhex(sys.argv[3])
sink = records.SessionSink("diagnostics", session, lambda line: sys.stdout.write(json.dumps(line) + "\\n"))
for record in records.decode_chunk("diagnostics", raw, session["session"], 0):
    sink.write(record)
sink.finish(complete=True)
`, SCRIPTS, JSON.stringify(session), Buffer.from(join(records)).toString('hex')], { encoding: 'utf8' });
}

test('a closed session keeps its last record, and an unclosed one drops it with a note', () => {
  const records = decodeChunk(join([record(21, 0, [1, 0, 0]), record(17, 0, [1])]), 0);
  const closed = sessionLines({ session: 7, records: 2, closed: true }, records);
  assert.deepEqual(closed.map(line => line.type), ['session', 'adsb', 'end']);
  const cut = sessionLines({ session: 7, records: 2, closed: false }, records);
  assert.deepEqual(cut.map(line => line.type), ['session', 'adsb', 'torn']);
  assert.equal(cut[2].index, 1);
});

test('each aircraft is one row, with its latest record and how many it wrote', () => {
  const lines = sessionLines({ session: 7, records: 5, closed: true }, decodeChunk(join([
    record(21, 0b0001_0000, [0x5e, 0x4d, 0x3c, 0, 0xb8, 0x0b, 0, 0]),
    record(3, 0),
    record(21, 0, [0x34, 0x12, 0xdd]),
    record(21, 0b0001_0000, [0x5e, 0x4d, 0x3c, 0, 0xa0, 0x0f, 0, 0]),
    record(17, 0, [5]),
  ]), 0));
  const aircraft = aircraftOf(lines);
  assert.deepEqual(aircraft.map(({ address, records, latest }) => [address, records, latest.index, latest.pressure_altitude_ft]), [
    [0x3c4d5e, 2, 3, 4000], [0xdd1234, 1, 2, undefined],
  ]);
  assert.deepEqual(typeCounts(lines), { adsb: 3, end: 1, gnss: 1 });
});
