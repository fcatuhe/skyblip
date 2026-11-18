import test from 'node:test';
import assert from 'node:assert/strict';

import { browserKeyFrom } from '#skyblip/trust';
import { asking, connected, settled } from './update_session.mjs';

async function trustedKey() {
  const keys = await crypto.subtle.generateKey({ name: 'Ed25519' }, false, ['sign', 'verify']);
  const key = await browserKeyFrom(keys, 'Chrome on Linux');
  return { key, load: async () => key };
}

test('a Go asks on its glass to pair, starts on the press, and reads back the Bravo it paired with', async () => {
  const { device, updater, until } = await connected();
  updater.pair(0x3f2a);
  await until(asking('pair'));
  assert.deepEqual(updater.state.asked, { command: 'pair', tag: 0x3f2a });
  device.press();
  const done = await until(state => state.notice?.key === 'paired' && state.pairedTag === 0x3f2a);
  assert.equal(done.phase, 'ready');
  assert.equal(done.notice.during, 'pair');
  assert.deepEqual(device.commands.slice(-2), ['pair', 'get']);
});

test('the press that lets a Go pair trusts the browser, and the Go then pairs with no press', async () => {
  const { key, load } = await trustedKey();
  const { device, updater, until } = await connected({ browserKey: load });
  updater.pair(0x3f2a);
  await until(asking('pair'));
  device.press();
  await until(state => state.notice?.key === 'paired');
  assert.equal(updater.state.trust, 'trusted');
  assert.ok(device.trustedBrowsers.some(({ label }) => label === key.label));

  updater.disconnect();
  await until(state => state.phase === 'offline');
  await updater.connect();
  await until(state => state.trust === 'trusted' && state.status);
  const phases = [];
  const paired = until(state => {
    phases.push(state.phase);
    return state.notice?.key === 'paired';
  });
  updater.pair(0x4b4b);
  await paired;
  assert.ok(!phases.includes('confirming'));
});

test('a Go that does not find its Bravo ends the pairing with its reason, during the pairing', async () => {
  const { device, updater, until } = await connected({ pairVerdict: 'not_found' });
  updater.pair(0x3f2a);
  await until(asking('pair'));
  device.press();
  assert.deepEqual((await until(settled)).notice, { key: 'not_found', during: 'pair' });
});

test('a Bravo that trusts this browser arms its acceptance, and one that does not is left to its own press', async () => {
  const { load } = await trustedKey();
  const trusted = await connected({ product: 'bravo', name: 'skyBlip Bravo 3F2A', browserKey: load });
  trusted.device.trust(trusted.device.proven);
  trusted.updater.disconnect();
  await trusted.until(state => state.phase === 'offline');
  await trusted.updater.connect();
  await trusted.until(state => state.trust === 'trusted' && state.status);
  trusted.updater.pair(0x1b2c);
  assert.deepEqual((await trusted.until(settled)).notice, { key: 'pair_armed', during: 'pair' });
  assert.equal(trusted.device.armedFor, 0x1b2c);

  const untrusted = await connected({ product: 'bravo', name: 'skyBlip Bravo 3F2A' });
  untrusted.updater.pair(0x1b2c);
  assert.deepEqual((await untrusted.until(settled)).notice, { key: 'untrusted', during: 'pair' });
  assert.ok(!untrusted.device.commands.includes('pair'));
});

test('nothing is asked in flight', async () => {
  const { device, updater, until } = await connected({ flight: 'airborne' });
  updater.pair(0x3f2a);
  assert.deepEqual((await until(settled)).notice, { key: 'in_flight', during: 'pair' });
  assert.ok(!device.commands.includes('pair'));
});
