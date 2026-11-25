import test from 'node:test';
import assert from 'node:assert/strict';

import { EQUIPMENT, addAircraft, airliner, callsign, nearbyAircraft } from '#skyblip/simulated_traffic';

const DRAWS = 2000;

function seeded(seed = 1) {
  let state = seed;
  return () => {
    state = (state * 1103515245 + 12345) % 2 ** 31;
    return state / 2 ** 31;
  };
}

const sample = (make, random = seeded()) => Array.from({ length: DRAWS }, () => make(random));
const rangeM = ({ northM, eastM }) => Math.hypot(northM, eastM);
const knots = ({ speedMps }) => speedMps * 1.943844;

test('nearby traffic flies within a few kilometres and a few hundred metres of own-ship, at light aircraft speeds', () => {
  for (const aircraft of sample(nearbyAircraft)) {
    assert.ok(rangeM(aircraft) >= 799 && rangeM(aircraft) <= 4801, `range ${rangeM(aircraft)}`);
    assert.ok(Math.abs(aircraft.upM) <= 600);
    assert.ok(knots(aircraft) >= 34 && knots(aircraft) <= 181, `speed ${knots(aircraft)}`);
    assert.ok(aircraft.address >= 1 && aircraft.address <= 0xffffff);
  }
});

// A cruising aircraft turns at most 2 deg/s, so a faster turn marks one of the thermalling fifth.
test('a fifth of nearby traffic thermals, the rest cruises', () => {
  const aircraft = sample(nearbyAircraft);
  const thermalling = aircraft.filter(({ turnDpsE1 }) => Math.abs(turnDpsE1) > 20).length / DRAWS;

  assert.ok(thermalling > 0.15 && thermalling < 0.25, `thermalling share ${thermalling}`);
  assert.ok(aircraft.every(({ turnDpsE1 }) => Math.abs(turnDpsE1) <= 150));
  assert.ok(aircraft.every(({ climbMmS }) => Math.abs(climbMmS) <= 8000));
});

test('an airliner is far, up to cruise height, fast and straight', () => {
  for (const aircraft of sample(airliner)) {
    assert.ok(rangeM(aircraft) >= 7999 && rangeM(aircraft) <= 30_001);
    assert.ok(aircraft.upM >= -600 && aircraft.upM <= 8500);
    assert.ok(knots(aircraft) >= 104 && knots(aircraft) <= 461);
    assert.deepEqual([aircraft.turnDpsE1, aircraft.climbMmS], [0, 0]);
  }
});

test('a callsign is a six-character registration under a European prefix', () => {
  for (const text of sample(callsign)) {
    assert.match(text, /^(F-[GHJC]|D-[EK]|G-|HB-|OO-|EC-|I-)[A-Z]+$/);
    assert.equal(text.length, 6);
  }
});

test('an aircraft reaches the firmware in the order simulator_add_aircraft takes its arguments', () => {
  const calls = [];
  const sim = { addAircraft: (...args) => { calls.push(args); return 4; } };
  const aircraft = { northM: 1, eastM: 2, upM: 3, speedMps: 4, trackDeg: 5, turnDpsE1: 6, climbMmS: 7, address: 8 };

  assert.equal(addAircraft(sim, aircraft, EQUIPMENT.alptas), 4);
  assert.deepEqual(calls, [[1, 2, 3, 4, 5, 6, 7, EQUIPMENT.alptas, 8]]);
});
