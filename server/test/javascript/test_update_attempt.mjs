import test from 'node:test';
import assert from 'node:assert/strict';

import { signedImage } from './fake_skyblip.mjs';

import { NEWER, IMAGE_BYTES, asking, settled, chosen } from './update_session.mjs';

const RUNNING = { major: 0, minor: 1, revision: 0, build: 12 };

test('an install names the version it asked for, and the restart it waits on', async () => {
  const { device, updater, until } = await chosen({ returnEveryMs: 0 });
  assert.equal(updater.state.attempt, null);
  updater.install();
  await until(asking('dfu'));
  assert.deepEqual(updater.state.attempt, { version: '0.2.0+15', restarted: false });
  device.press();
  const rebooting = await until(state => state.phase === 'rebooting');
  assert.deepEqual(rebooting.attempt, { version: '0.2.0+15', restarted: true });
  device.running = '0.2.0.15';
  const back = await until(state => state.phase === 'ready' && state.running);
  assert.deepEqual(back.attempt, { version: '0.2.0+15', restarted: true });
  updater.disconnect();
});

// The full image of the running version shares its version, so the version alone cannot say the unit restarted on it.
test('an install of the running version refused on the glass never reads as restarted', async () => {
  const { device, updater, until } = await chosen({ running: '0.1.0.12' });
  await updater.choose(signedImage(RUNNING, IMAGE_BYTES), 'skyblip-go-0.1.0+12.full.signed.bin');
  updater.install();
  await until(asking('dfu'));
  device.refuse();
  const refused = await until(settled);
  assert.deepEqual(refused.attempt, { version: '0.1.0+12', restarted: false });
});

test('a new image ends the install before it, so the next one starts afresh', async () => {
  const { device, updater, until } = await chosen({ returnEveryMs: 0 });
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(state => state.phase === 'rebooting');
  device.running = '0.2.0.15';
  await until(state => state.phase === 'ready' && state.running);
  await updater.choose(signedImage(NEWER, IMAGE_BYTES), 'skyblip_go-0.2.0+15.full.signed.bin');
  assert.equal(updater.state.attempt, null);
  updater.install();
  assert.deepEqual(updater.state.attempt, { version: '0.2.0+15', restarted: false });
  updater.clearImage();
  assert.equal(updater.state.attempt, null);
  updater.disconnect();
});

test('an install the page refuses before asking starts no attempt', async () => {
  const { updater } = await chosen({ downgrade: false, running: '0.3.0.1' });
  updater.install();
  assert.equal(updater.state.notice.key, 'older');
  assert.equal(updater.state.attempt, null);
});
