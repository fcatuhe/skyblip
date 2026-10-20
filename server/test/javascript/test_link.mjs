import test from 'node:test';
import assert from 'node:assert/strict';

import { FrameError, decodeFrame, encodeFrame, hex, printable } from '#skyblip/link';
import { LINK_REGISTRY } from '#skyblip/link_registry';

const bytes = text => Uint8Array.from(text.match(/../g) ?? [], pair => parseInt(pair, 16));

test('the widest status is the twenty bytes the firmware pins', () => {
  const status = {
    command: 'status', flight: 'airborne', upload: false, battery_percent: 100, charging: false, power_level: 'critical',
    went_dark_flat: false, die_temperature_c: -3277,
  };
  const frame = encodeFrame(status);
  assert.equal(hex(frame), 'a800050102' + '02f4031864' + '04f40502' + '06f407390ccc');
  assert.deepEqual(decodeFrame(frame), status);
});

test('a prompt and a dfu write are these bytes', () => {
  const prompt = { command: 'acknowledgement', acknowledge: false, pending: true, reason: 'confirm_dfu' };
  assert.equal(hex(encodeFrame(prompt)), 'a4000001f402f50301');
  assert.equal(hex(encodeFrame({ command: 'dfu', version: '0.2.0+15' })), 'a2000b0168302e322e302b3135');
});

test('keys leave ascending whatever order they were given in', () => {
  const frame = encodeFrame({ callsign: 'F-JXYZ', units: 'metric', command: 'set' });
  assert.equal(hex(frame), 'a3000407010866462d4a58595a');
});

test('a set and a defaults reply share one key table', () => {
  const defaults = { command: 'defaults', aircraft_type: 1, alarm_enabled: true, alarm_volume: 'loud', units: 'nautical', callsign: '' };
  assert.deepEqual(encodeFrame(defaults).slice(3), encodeFrame({ ...defaults, command: 'set' }).slice(3));
});

test('an enumeration goes out by name or by code, and comes back by name', () => {
  assert.deepEqual(encodeFrame({ command: 'set', units: 1 }), encodeFrame({ command: 'set', units: 'metric' }));
  assert.equal(decodeFrame(encodeFrame({ command: 4, units: 1 })).units, 'metric');
});

test('a byte string goes out as bytes or as hex, and comes back as bytes', () => {
  const nonce = '00'.repeat(16);
  const frame = decodeFrame(encodeFrame({ command: 'challenge', nonce, server_key_id: '890026B1' }));
  assert.deepEqual(frame.server_key_id, bytes('890026b1'));
  assert.equal(printable(frame), `{"command":"challenge","nonce":"${nonce}","server_key_id":"890026b1"}`);
  assert.deepEqual(encodeFrame(frame), encodeFrame({ command: 'challenge', nonce, server_key_id: '890026b1' }));
});

test('what the registry does not hold is refused before anything is sent', () => {
  for (const fields of [
    { command: 'apply' },
    { units: 'metric' },
    { command: 'set', region: 3 },
    { command: 'set', units: 'imperial' },
    { command: 'set', aircraft_type: -1 },
    { command: 'set', alarm_enabled: 1 },
    { command: 'unlock', serial: 1, signature: '00' },
    { command: 'register', server_nonce: 'not hex' },
  ]) {
    assert.throws(() => encodeFrame(fields), FrameError, JSON.stringify(fields));
  }
});

test('a code the registry does not hold stays a number for a person to read', () => {
  assert.deepEqual(decodeFrame(bytes('a300000318c80901')), { command: 'acknowledgement', reason: 200, 9: 1 });
  assert.deepEqual(decodeFrame(bytes('a100181e')), { command: 30 });
});

test('what the deterministic subset does not allow is refused', () => {
  for (const frame of [
    'a1001805', 'a1180005', 'bf0005ff', 'a1007f6161ff', 'a100f93c00', 'a100f6', 'a100c105', 'a201050005', 'a201050106',
    'a12005', 'a1000500', 'a1006261', 'a20005', '05',
  ]) {
    assert.throws(() => decodeFrame(bytes(frame)), Error, frame);
  }
});

const SAMPLES = { unsigned: 70000, integer: -300, boolean: true, text: 'D-KXYZ', unsigned_array: [0, 24, 70000] };

function sampleOf(key) {
  if (key.type === 'enum') return LINK_REGISTRY.enums[key.enum].values.at(-1).name;
  if (key.type === 'bytes') return Uint8Array.from({ length: key.bytes ?? 30 }, (_, at) => at);
  return SAMPLES[key.type];
}

test('every frame and every key round-trips', () => {
  for (const frame of LINK_REGISTRY.frames) {
    const fields = Object.fromEntries(frame.keys.map(key => [key.name, sampleOf(key)]));
    assert.deepEqual(decodeFrame(encodeFrame({ ...fields, command: frame.command })), { ...fields, command: frame.command }, frame.command);
  }
});
