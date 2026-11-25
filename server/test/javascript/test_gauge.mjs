import test from 'node:test';
import assert from 'node:assert/strict';

import { lawPosition } from '../../app/javascript/controllers/simulator/gauge.js';

test('a law interpolates between its breakpoints and holds its ends beyond them', () => {
  const vsi = { points: [[-2000, -260], [-1000, -180], [0, -90], [1000, 0], [2000, 80]] };

  assert.equal(lawPosition(vsi, 500), -45);
  assert.equal(lawPosition(vsi, 1500), 40);
  assert.equal(lawPosition(vsi, -1500), -220);
  assert.equal(lawPosition(vsi, 3000), 80);
  assert.equal(lawPosition(vsi, -3000), -260);
});

test('a turning law starts over each cycle, so the altimeter needle reads 4500 ft as 500 ft', () => {
  const needle = { cycle: 1000, points: [[0, 0], [1000, 360]] };
  const card = { cycle: 360, points: [[0, 0], [360, -360]] };

  assert.equal(lawPosition(needle, 4500), 180);
  assert.equal(lawPosition(needle, 900), 324);
  assert.equal(lawPosition(card, 450), -90);
  assert.equal(lawPosition(card, -90), -270);
});
