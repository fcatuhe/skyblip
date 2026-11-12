import test from 'node:test';
import assert from 'node:assert/strict';

import { asking, connected } from './update_session.mjs';

const bravo = (options = {}) => connected({ product: 'skyblip_bravo', ...options });

test('a capture is asked on Bravo and armed by its two presses', async () => {
  const { device, updater, until } = await bravo();
  updater.orderCapture('power_run');
  await until(asking('capture'));
  assert.equal(device.staged, 'power_run');
  device.press();
  const armed = await until(state => state.notice?.key === 'capture_armed');
  assert.equal(armed.phase, 'ready');
  assert.equal(armed.notice.during, 'capture');
  assert.equal(device.capturing, true);
});

test('a stop allowed on Bravo says the capture stopped', async () => {
  const { device, updater, until } = await bravo();
  device.capturing = true;
  updater.orderCapture('stop');
  await until(asking('capture'));
  device.press();
  await until(state => state.notice?.key === 'capture_stopped');
  assert.equal(device.capturing, false);
});

test('one press on Bravo reads as cancelled, under the capture control', async () => {
  const { device, updater, until } = await bravo();
  updater.orderCapture('full');
  await until(asking('capture'));
  device.refuse();
  const refused = await until(state => state.notice?.key === 'cancelled');
  assert.deepEqual(refused.notice, { key: 'cancelled', during: 'capture' });
  assert.equal(refused.refusedOnDevice, true);
  assert.equal(device.capturing, false);
});

test('an arm while a capture runs is refused capturing, and nothing is asked', async () => {
  const { device, updater, until } = await bravo();
  device.capturing = true;
  updater.orderCapture('full');
  const refused = await until(state => state.notice?.key === 'capturing');
  assert.equal(refused.phase, 'ready');
  assert.equal(device.pending, null);
});

test('in flight the page refuses before it asks, as the unit would', async () => {
  const { device, updater, until } = await bravo({ flight: 'airborne' });
  await until(state => state.status?.flight === 'airborne');
  updater.orderCapture('full');
  assert.deepEqual(updater.state.notice, { key: 'in_flight', during: 'capture' });
  assert.equal(device.commands.includes('capture'), false);
});

test('a Go answers unknown_command, and the page says so under the capture control', async () => {
  const { updater, until } = await connected();
  updater.orderCapture('full');
  const refused = await until(state => state.notice?.key === 'unknown_command');
  assert.deepEqual(refused.notice, { key: 'unknown_command', during: 'capture' });
});
