import test from 'node:test';
import assert from 'node:assert/strict';

import { goAlarmOf, heardSettings, settingsOf, statusOf } from '#skyblip/settings';

const GO = { aircraft_type: 1, callsign: 'F-JXYZ', aircraft_address: '', units: 'nautical' };
const BRAVO = { alarm_enabled: true, alarm_volume: 'loud', aircraft_address: '' };

test('status reads the transponder Bravo hears in the shape the form shows', () => {
  const status = statusOf({ command: 'status', flight: 'ground', own_transponder: 0x3a4b5c, own_transponder_callsign: 'F-JABC', own_transponder_category: 4 });
  assert.deepEqual(status.ownTransponder, { aircraft_address: '3A4B5C', callsign: 'F-JABC', aircraft_type: 4 });
  assert.deepEqual(statusOf({ command: 'status', own_transponder: 0x3a4b5c }).ownTransponder, { aircraft_address: '3A4B5C', callsign: '', aircraft_type: 0 });
  assert.equal(statusOf({ command: 'status', flight: 'airborne' }).ownTransponder, null);
});

test('status says whether a Go holds a Bravo, and what it makes of it', () => {
  assert.equal(statusOf({ command: 'status', flight: 'ground', bravo: 'live' }).bravo, 'live');
  assert.equal(statusOf({ command: 'status', flight: 'ground' }).bravo, null);
});

test('a Go takes the address, the callsign and the type, as its AIRCRAFT menu does', () => {
  const heard = { aircraft_address: '3A4B5C', callsign: 'F-JABC', aircraft_type: 4 };
  assert.deepEqual(heardSettings(heard, GO), { aircraft_address: '3A4B5C', callsign: 'F-JABC', aircraft_type: 4 });
});

// Another aircraft's callsign must not go on air beside this one's address.
test('a transponder with no identification clears the callsign, unless its address is already set', () => {
  const heard = { aircraft_address: '3A4B5C', callsign: '', aircraft_type: 0 };
  assert.deepEqual(heardSettings(heard, GO), { aircraft_address: '3A4B5C', callsign: '' });
  assert.deepEqual(heardSettings(heard, { ...GO, aircraft_address: '3A4B5C' }), { aircraft_address: '3A4B5C' });
});

test('a Bravo takes the address alone, since it stores no callsign and no type', () => {
  const heard = { aircraft_address: '3A4B5C', callsign: 'F-JABC', aircraft_type: 4 };
  assert.deepEqual(heardSettings(heard, BRAVO), { aircraft_address: '3A4B5C' });
});

// A phone set while paired must not leave Bravo disagreeing with its Go, so the form never sends it.
test('a paired Bravo shows its Go\'s alarm and holds no alarm of its own to send', () => {
  const paired = { command: 'config', aircraft_address: 0, alarm: 'soft', alarm_from_go: true };
  assert.deepEqual(settingsOf(paired), { aircraft_address: '' });
  assert.equal(goAlarmOf(paired), 'soft');

  const alone = { command: 'config', aircraft_address: 0, alarm: 'visual', alarm_from_go: false };
  assert.deepEqual(settingsOf(alone), { aircraft_address: '', alarm: 'visual' });
  assert.equal(goAlarmOf(alone), null);
  assert.equal(goAlarmOf({ command: 'config', alarm: 'loud' }), null);
});
