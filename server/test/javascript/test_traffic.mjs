import test from 'node:test';
import assert from 'node:assert/strict';

import { decodeGdl90 } from '#skyblip/gdl90';
import { distanceM, offset } from '#skyblip/geo';
import { nmeaChecksum, parseNmea } from '#skyblip/nmea';
import { LIVE_MS, TrafficPicture } from '#skyblip/traffic';

const MERIGNAC = { latitude: 44.8283, longitude: -0.7156 };
const sentence = body => parseNmea(`$${body}*${nmeaChecksum(body)}`);

function report(id, address, { latitude, longitude }, altitudeFt, callsign = '') {
  const message = new Uint8Array(28);
  message[0] = id;
  message.set([address >> 16, (address >> 8) & 0xff, address & 0xff], 2);
  const put = (at, degrees) => {
    const raw = Math.round((degrees * 2 ** 23) / 180) & 0xffffff;
    message.set([raw >> 16, (raw >> 8) & 0xff, raw & 0xff], at);
  };
  put(5, latitude);
  put(8, longitude);
  const altitude = (altitudeFt + 1000) / 25;
  message.set([altitude >> 4, ((altitude & 0x0f) << 4) | 0x09], 11);
  message.set([0x07, 0xb0, 0x01, 0x40, 0x01], 14);
  message.set(new TextEncoder().encode(callsign.padEnd(8)), 19);
  return decodeGdl90(message);
}

function flying() {
  const picture = new TrafficPicture(['retro', 'bravo']);
  picture.record('bravo', sentence('GPRMC,101530.00,A,4449.698,N,00042.936,W,95.2,271.5,201126,,,A'), 0);
  picture.record('retro', report(10, 0xabcdef, MERIGNAC, 2000), 0);
  return picture;
}

test('one aircraft heard by both receivers is one row, placed by each', () => {
  const picture = flying();
  const there = offset(MERIGNAC, 3000, 4000);
  picture.record('retro', report(20, 0x3c6589, there, 3000, 'DLH4AB'), 1000);
  picture.record('bravo', sentence('PFLAA,0,3000,4000,305,1,3C6589,90,,60,1.5,9,0,1,'), 3500);

  const [row] = picture.aircraft(4000);
  assert.deepEqual([row.address, row.callsign, row.firstBy, row.leadMs], ['3C6589', 'DLH4AB', 'retro', 2500]);
  assert.deepEqual(row.live, ['retro', 'bravo']);
  assert.ok(Math.abs(row.by.retro.report.distanceM - 5000) < 5);
  assert.equal(row.by.bravo.report.distanceM, 5000);
  assert.ok(distanceM(row.by.bravo.report, there) < 5);
  assert.equal(row.by.retro.report.relativeVerticalFt, 1000);
});

test('an aircraft heard by one receiver only counts against the other', () => {
  const picture = flying();
  picture.record('bravo', sentence('PFLAA,0,1000,0,0,1,AAAAAA,,,,,8'), 0);
  picture.record('bravo', sentence('PFLAA,0,9000,0,0,1,BBBBBB,,,,,8'), 0);
  picture.record('retro', report(20, 0xbbbbbb, offset(MERIGNAC, 9000, 0), 2000), 0);

  const tally = picture.tally(0);
  assert.deepEqual([tally.total, tally.everyone, tally.only.bravo, tally.only.retro], [2, 1, 1, 0]);
  assert.equal(tally.farthestM.bravo, 9000);
});

test('a target silent for longer than LIVE_MS sinks below the live ones', () => {
  const picture = flying();
  picture.record('bravo', sentence('PFLAA,0,500,0,0,1,AAAAAA,,,,,8'), 0);
  picture.record('bravo', sentence('PFLAA,0,8000,0,0,1,BBBBBB,,,,,8'), LIVE_MS);

  const rows = picture.aircraft(LIVE_MS + 1);
  assert.deepEqual(rows.map(row => row.address), ['BBBBBB', 'AAAAAA']);
  assert.deepEqual(rows[1].live, []);
});

test('a PFLAA before any fix has a distance but no place on the chart', () => {
  const picture = new TrafficPicture(['bravo']);
  picture.record('bravo', sentence('PFLAA,0,300,400,0,1,AAAAAA,,,,,8'), 0);
  const { report } = picture.source('bravo').targets.get('AAAAAA');
  assert.equal(report.distanceM, 500);
  assert.equal(report.latitude, undefined);
});
