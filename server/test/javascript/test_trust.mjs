import test from 'node:test';
import assert from 'node:assert/strict';

import { browserKeyFrom, idOf, labelOf, storedBrowserKey } from '#skyblip/trust';
import { IMAGE_BYTES, NEWER, asking, chosen, connected } from './update_session.mjs';
import { signedImage } from './fake_skyblip.mjs';

async function browserKey(label = 'Chrome on Android') {
  const keys = await crypto.subtle.generateKey({ name: 'Ed25519' }, false, ['sign', 'verify']);
  const key = await browserKeyFrom(keys, label);
  return { key, load: async () => key };
}

const saved = state => state.notice?.key === 'saved';

async function reconnect(updater, until) {
  updater.disconnect();
  await until(state => state.phase === 'offline');
  await updater.connect();
  return until(state => state.status && state.running);
}

async function trustedSession(options = {}) {
  const { key, load } = await browserKey();
  const link = await connected({ browserKey: load, ...options });
  link.updater.saveSettings({ ...link.updater.state.settings, units: 'metric' });
  await link.until(asking('set'));
  link.device.press();
  await link.until(saved);
  return { key, ...link };
}

test('a browser two presses approved on a save is trusted, and its next save asks nothing', async () => {
  const { key, load } = await browserKey();
  const { device, updater, until } = await connected({ browserKey: load });
  assert.equal(updater.state.trust, 'proven');

  updater.saveSettings({ ...updater.state.settings, units: 'metric' });
  await until(asking('set'));
  device.press();
  const trusted = await until(saved);
  assert.equal(trusted.trust, 'trusted');
  assert.deepEqual(device.trustedBrowsers, [{ id: idOf(key.publicKey), label: 'Chrome on Android' }]);

  const back = await reconnect(updater, until);
  assert.equal(back.trust, 'trusted');
  updater.saveSettings({ ...back.settings, units: 'nautical' });
  await until(state => saved(state) && state.settings.units === 'nautical');
  assert.equal(device.pending, null);
  assert.equal(device.stored.units, 'nautical');
});

test('a trusted browser saves in flight, and still waits for the landing to install', async () => {
  const { device, updater, until } = await trustedSession();
  await reconnect(updater, until);
  device.takeOff();
  await until(state => state.status?.flight === 'airborne');

  updater.saveSettings({ ...updater.state.settings, units: 'nautical' });
  await until(state => saved(state) && state.settings.units === 'nautical');

  await updater.choose(signedImage(NEWER, IMAGE_BYTES), 'skyblip_go-0.2.0+15.signed.bin');
  await updater.install();
  assert.equal(updater.state.notice.key, 'in_flight');
  assert.equal(device.commands.includes('dfu'), false);
});

test('a trusted browser installs with no prompt on the glass', async () => {
  const { device, updater, until } = await trustedSession();
  await reconnect(updater, until);
  await updater.choose(signedImage(NEWER, IMAGE_BYTES), 'skyblip_go-0.2.0+15.signed.bin');
  updater.install();
  await until(state => state.phase === 'installing');
  assert.equal(device.approved, '0.2.0+15');
});

test('a dfu two presses approved trusts the browser too', async () => {
  const { load } = await browserKey();
  const { device, updater, until } = await chosen({ browserKey: load });
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(state => state.phase === 'installing');
  assert.equal(device.trustedBrowsers.length, 1);
});

test('a refused save trusts no browser', async () => {
  const { load } = await browserKey();
  const { device, updater, until } = await connected({ browserKey: load });
  updater.saveSettings({ ...updater.state.settings, units: 'metric' });
  await until(asking('set'));
  device.refuse();
  const refused = await until(state => state.notice?.key === 'cancelled');
  assert.equal(refused.trust, 'proven');
  assert.deepEqual(device.trustedBrowsers, []);
});

test('firmware that knows no trusted browsers is asked with presses, as before', async () => {
  const { load } = await browserKey();
  const { device, updater, until } = await connected({ browserKey: load, knowsBrowsers: false });
  assert.equal(updater.state.trust, null);
  updater.saveSettings({ ...updater.state.settings, units: 'metric' });
  await until(asking('set'));
  assert.equal(device.pending, 'set');
});

test('a trusted browser lists the browsers, finds itself, and forgets the others', async () => {
  const { key, device, updater, until } = await trustedSession();
  device.trustedBrowsers.unshift({ id: '0102030405060708', label: 'Edge on Windows' }, { id: '1112131415161718', label: '' });
  await updater.listBrowsers();
  assert.deepEqual(updater.state.browsers, [
    { id: '0102030405060708', label: 'Edge on Windows', self: false },
    { id: '1112131415161718', label: '', self: false },
    { id: idOf(key.publicKey), label: 'Chrome on Android', self: true },
  ]);

  await updater.forgetBrowsers(['0102030405060708', '1112131415161718']);
  assert.deepEqual(updater.state.browsers, [{ id: idOf(key.publicKey), label: 'Chrome on Android', self: true }]);
  assert.equal(device.trustedBrowsers.length, 1);
  await until(state => state.phase === 'ready');
});

test('a browser that forgets itself is trusted no more, and the next save asks again', async () => {
  const { key, device, updater, until } = await trustedSession();
  await updater.forgetBrowsers([idOf(key.publicKey)]);
  assert.equal(updater.state.trust, null);
  assert.equal(updater.state.browsers, null);
  assert.deepEqual(device.trustedBrowsers, []);

  updater.saveSettings({ ...updater.state.settings, units: 'nautical' });
  await until(asking('set'));
});

test('an untrusted browser is not shown the list', async () => {
  const { load } = await browserKey();
  const { device, updater } = await connected({ browserKey: load });
  await updater.listBrowsers();
  assert.equal(updater.state.browsers, null);
  assert.equal(device.commands.includes('browsers'), false);
});

test('a browser names itself by its brand and platform, in at most 24 bytes', () => {
  const agent = platform => ({ userAgentData: { brands: [{ brand: 'Not.A/Brand' }, { brand: 'Chromium' }, { brand: 'Google Chrome' }], platform } });
  assert.equal(labelOf(agent('Android')), 'Chrome on Android');
  assert.equal(labelOf(agent('A platform with a long name')), 'Chrome on A platform wit');
  assert.equal(labelOf({}), 'Browser');
});

test('a browser with no IndexedDB has no key', async () => {
  assert.equal(await storedBrowserKey(), null);
});
