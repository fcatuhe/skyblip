import test from 'node:test';
import assert from 'node:assert/strict';

import { decodeGdl90, gdl90Crc, gdl90Frames } from '#skyblip/gdl90';

const bytes = text => Uint8Array.from(text.split(' '), pair => parseInt(pair, 16));

function frameOf(message) {
  const crc = gdl90Crc(message);
  const body = [...message, crc & 0xff, crc >> 8].flatMap(byte => (byte === 0x7e || byte === 0x7d ? [0x7d, byte ^ 0x20] : [byte]));
  return Uint8Array.from([0x7e, ...body, 0x7e]);
}

const collect = () => {
  const messages = [];
  const bad = [];
  return { messages, bad, feed: gdl90Frames(message => messages.push(message), frame => bad.push(frame)) };
};

// The heartbeat example of the GDL 90 specification, 560-1058-00 Rev A.
test('the heartbeat of the specification passes its CRC', () => {
  const { messages, bad, feed } = collect();
  feed(bytes('7E 00 81 41 DB D0 08 02 B3 8B 7E'));
  assert.equal(bad.length, 0);
  assert.deepEqual(messages[0], { kind: 'heartbeat', gpsValid: true, maintenanceRequired: false, batteryLow: false, utcValid: true, secondsOfDay: 53467 });
});

// The traffic report example of the same specification: N825V near Salem, Oregon.
test('the traffic report of the specification decodes to its documented values', () => {
  const traffic = decodeGdl90(bytes('14 00 AB 45 49 1F EF 15 A8 89 78 0F 09 A9 07 B0 01 20 01 4E 38 32 35 56 20 20 20 00'));
  assert.equal(traffic.kind, 'traffic');
  assert.equal(traffic.address, 'AB4549');
  assert.equal(traffic.addressType, 'ADS-B ICAO');
  assert.ok(Math.abs(traffic.latitude - 44.90708) < 1e-4);
  assert.ok(Math.abs(traffic.longitude + 122.99488) < 1e-4);
  assert.deepEqual([traffic.pressureAltitudeFt, traffic.airborne, traffic.trackType], [5000, true, 'true track']);
  assert.deepEqual([traffic.nic, traffic.nacp, traffic.groundSpeedKt, traffic.climbFpm, traffic.trackDeg], [10, 9, 123, 64, 45]);
  assert.deepEqual([traffic.emitterCategory, traffic.callsign, traffic.emergencyCode], ['light', 'N825V', 0]);
});

test('escaped bytes are restored and a frame cut across reads arrives whole', () => {
  const message = bytes('0B 7E 7D 00 0A');
  const frame = frameOf(message);
  const { messages, feed } = collect();
  feed(frame.subarray(0, 4));
  feed(frame.subarray(4));
  assert.deepEqual(messages, [{ kind: 'ownship_altitude', geometricAltitudeFt: 0x7e7d * 5, verticalWarning: false, verticalFigureOfMeritM: 10 }]);
});

test('a frame whose CRC does not match is refused, and the next one still decodes', () => {
  const { messages, bad, feed } = collect();
  feed(bytes('7E 00 81 41 DB D0 08 02 B3 8C 7E'));
  feed(frameOf(bytes('00 81 41 DB D0 08 02')));
  assert.equal(bad.length, 1);
  assert.equal(messages.length, 1);
});

test('a report without position, altitude or velocity says so rather than zero', () => {
  const traffic = decodeGdl90(bytes('14 00 AB 45 49 00 00 00 00 00 00 FF F0 00 FF F8 00 00 00 20 20 20 20 20 20 20 20 00'));
  assert.deepEqual([traffic.latitude, traffic.longitude, traffic.pressureAltitudeFt], [null, null, null]);
  assert.deepEqual([traffic.groundSpeedKt, traffic.climbFpm, traffic.trackDeg, traffic.callsign], [null, null, null, null]);
});

test('NMEA text read as GDL 90 yields no frame at all', () => {
  const { messages, bad, feed } = collect();
  feed(new TextEncoder().encode('$PGRMZ,4100,F,3*2C\r\n'));
  assert.deepEqual([messages.length, bad.length], [0, 0]);
});
