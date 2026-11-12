import test from 'node:test';
import assert from 'node:assert/strict';

import { nmeaChecksum, nmeaLines, parseNmea } from '#skyblip/nmea';

const sentence = body => `$${body}*${nmeaChecksum(body)}`;

test('a skyBlip PFLAA for an ADS-B target carries its callsign, NoTrack and Source', () => {
  const pflaa = parseNmea(sentence('PFLAA,0,-1234,5678,-305,1,3C6589!DLH4AB,271,,123,-2.5,9,0,1,'));
  assert.equal(pflaa.kind, 'pflaa');
  assert.equal(pflaa.address, '3C6589');
  assert.equal(pflaa.callsign, 'DLH4AB');
  assert.deepEqual([pflaa.northM, pflaa.eastM, pflaa.verticalM], [-1234, 5678, -305]);
  assert.deepEqual([pflaa.trackDeg, pflaa.turnRateDegS, pflaa.groundSpeedMs, pflaa.climbMs], [271, null, 123, -2.5]);
  assert.deepEqual([pflaa.aircraftType, pflaa.noTrack, pflaa.source, pflaa.rssiDbm], ['jet aircraft', 0, 'ADS-B', null]);
});

test('a PFLAA of the eleven original fields leaves the newer ones empty', () => {
  const pflaa = parseNmea(sentence('PFLAA,2,100,-50,30,2,dd1234,90,3,25,1.2,1'));
  assert.equal(pflaa.address, 'DD1234');
  assert.equal(pflaa.callsign, null);
  assert.equal(pflaa.alarmLevel, 2);
  assert.equal(pflaa.aircraftType, 'glider');
  assert.equal(pflaa.source, null);
});

test('a sentence with a wrong checksum or no checksum is refused', () => {
  assert.equal(parseNmea('$PFLAA,0,1,2,3,1,ABCDEF,,,,,8*00'), null);
  assert.equal(parseNmea('$PFLAA,0,1,2,3,1,ABCDEF,,,,,8'), null);
  assert.equal(parseNmea('garbage'), null);
});

test('RMC and GGA from any talker place own position in signed degrees', () => {
  const rmc = parseNmea(sentence('GNRMC,101530.00,A,4449.698,N,00042.936,W,95.2,271.5,201126,,,A'));
  assert.equal(rmc.valid, true);
  assert.ok(Math.abs(rmc.latitude - 44.82830) < 1e-5);
  assert.ok(Math.abs(rmc.longitude + 0.71560) < 1e-5);
  assert.deepEqual([rmc.groundSpeedKt, rmc.trackDeg], [95.2, 271.5]);

  const gga = parseNmea(sentence('GPGGA,101530.00,4449.698,N,00042.936,W,1,09,0.9,1250.4,M,49.1,M,,'));
  assert.deepEqual([gga.quality, gga.satellites, gga.altitudeM, gga.geoidSeparationM], [1, 9, 1250.4, 49.1]);
});

test('PFLAU and PGRMZ give the receiver status and pressure altitude', () => {
  const pflau = parseNmea(sentence('PFLAU,3,0,2,1,1,-45,2,-100,1000,DDA5BA'));
  assert.deepEqual([pflau.rx, pflau.gps, pflau.relativeBearingDeg, pflau.address], [3, 2, -45, 'DDA5BA']);
  assert.equal(parseNmea(sentence('PGRMZ,4100,F,3')).pressureAltitudeFt, 4100);
  assert.deepEqual(parseNmea(sentence('LK8EX1,101325,99,0,99,1082')), { kind: 'other', sentence: 'LK8EX1' });
});

test('lines split across serial reads arrive whole', () => {
  const lines = [];
  const feed = nmeaLines(line => lines.push(line));
  const bytes = new TextEncoder().encode('$PGRMZ,41');
  feed(bytes);
  feed(new TextEncoder().encode('00,F,3*2C\r\n$PFL'));
  assert.deepEqual(lines, ['$PGRMZ,4100,F,3*2C']);
});
