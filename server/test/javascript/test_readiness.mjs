import test from 'node:test';
import assert from 'node:assert/strict';

import { BluetoothReadiness, browserSupport, onDesktopLinux } from '#skyblip/readiness';

const CHROME = [{ brand: 'Chromium', version: '142' }, { brand: 'Google Chrome', version: '142' }];

function browser({ platform = 'Linux', brands = CHROME, bluetooth = null } = {}) {
  return { userAgent: 'Mozilla/5.0 (X11; Linux x86_64)', userAgentData: { platform, brands, mobile: false }, ...(bluetooth && { bluetooth }) };
}

function adapter({ present = true, getDevices = false } = {}) {
  const bluetooth = new EventTarget();
  bluetooth.getAvailability = async () => present;
  bluetooth.requestDevice = async () => null;
  if (getDevices) bluetooth.getDevices = async () => [];
  bluetooth.plug = value => bluetooth.dispatchEvent(Object.assign(new Event('availabilitychanged'), { value }));
  return bluetooth;
}

async function settled(readiness) {
  readiness.listen(() => {});
  await new Promise(resolve => setImmediate(resolve));
  return readiness;
}

test('Chrome on Linux without the flag lacks Web Bluetooth, and the adapter waits for it', async () => {
  const readiness = await settled(new BluetoothReadiness(browser()));
  assert.deepEqual(readiness.checks, { flag: 'missing', adapter: 'unknown', remember: 'unknown' });
  assert.equal(readiness.ready, false);
  assert.equal(readiness.fixable, true);
});

test('Chrome on Linux with the flag and an adapter is ready, and names the optional flag still off', async () => {
  const readiness = await settled(new BluetoothReadiness(browser({ bluetooth: adapter() })));
  assert.deepEqual(readiness.checks, { flag: 'ok', adapter: 'ok', remember: 'missing' });
  assert.equal(readiness.ready, true);
  assert.equal(readiness.fixable, false);
});

test('an adapter plugged in later clears the last line and tells the listeners', async () => {
  const bluetooth = adapter({ present: false, getDevices: true });
  const readiness = new BluetoothReadiness(browser({ bluetooth }));
  const heard = [];
  readiness.listen(each => heard.push(each.checks.adapter));
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(readiness.checks, { flag: 'ok', adapter: 'missing', remember: 'ok' });
  assert.equal(readiness.ready, false);
  assert.equal(readiness.fixable, true);

  bluetooth.plug(true);
  assert.deepEqual(heard, ['missing', 'ok']);
  assert.equal(readiness.ready, true);
});

test('an adapter not answered yet does not hold the connect button back', () => {
  const readiness = new BluetoothReadiness(browser({ platform: 'macOS', bluetooth: adapter() }));
  assert.deepEqual(readiness.checks, { flag: null, adapter: 'unknown', remember: 'missing' });
  assert.equal(readiness.ready, true);
});

test('a browser no flag fixes names nothing to switch on', async () => {
  const safari = await settled(new BluetoothReadiness({ userAgent: 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) Safari/605.1.15' }));
  assert.deepEqual(safari.checks, { flag: null, adapter: null, remember: null });
  assert.equal(safari.ready, false);
  assert.equal(safari.fixable, false);

  const firefox = new BluetoothReadiness({ userAgent: 'Mozilla/5.0 (X11; Linux x86_64; rv:140.0) Gecko/20100101 Firefox/140.0' });
  assert.equal(firefox.fixable, false);
});

test('only a desktop Linux counts as Linux', () => {
  assert.equal(onDesktopLinux({ userAgentData: { platform: 'Linux' } }), true);
  assert.equal(onDesktopLinux({ userAgentData: { platform: 'Android' } }), false);
  assert.equal(onDesktopLinux({ userAgentData: { platform: 'Chrome OS' } }), false);
  assert.equal(onDesktopLinux({ userAgent: 'Mozilla/5.0 (Linux; Android 14; Pixel 8)' }), false);
  assert.equal(onDesktopLinux({ userAgent: 'Mozilla/5.0 (X11; Linux x86_64; rv:140.0) Firefox/140.0' }), true);
});

test('installing needs Web Serial and the folder chooser, which only Chrome and Edge on a computer have', () => {
  assert.equal(browserSupport({ navigator: { serial: {} }, showDirectoryPicker: () => {} }), 'usb');
  assert.equal(browserSupport({ navigator: { userAgentData: { mobile: true } } }), 'phone');
  assert.equal(browserSupport({ navigator: { userAgent: 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X)' } }), 'phone');
  assert.equal(browserSupport({ navigator: { userAgent: 'Mozilla/5.0 (X11; Linux x86_64; rv:140.0) Gecko/20100101 Firefox/140.0' } }), 'other');
});

// Samsung Internet on an Android tablet offered Install, and its port chooser closed empty within 17 ms, six times over.
test('a phone or a tablet installs from a computer, even when its browser has Web Serial and the folder chooser', () => {
  const usb = { serial: {} };
  const scope = navigator => ({ navigator: { ...usb, ...navigator }, showDirectoryPicker: () => {} });
  assert.equal(browserSupport(scope({ userAgentData: { mobile: false, platform: 'Android' } })), 'phone');
  assert.equal(browserSupport(scope({ userAgentData: { mobile: true, platform: 'Android' } })), 'phone');
  assert.equal(browserSupport(scope({ userAgent: 'Mozilla/5.0 (Linux; Android 14; SM-X710) AppleWebKit/537.36 SamsungBrowser/30.0 Safari/537.36' })), 'phone');
  assert.equal(browserSupport(scope({ userAgentData: { mobile: false, platform: 'Windows' } })), 'usb');
});
