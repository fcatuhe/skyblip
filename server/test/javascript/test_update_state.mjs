import test from 'node:test';
import assert from 'node:assert/strict';

import {
  BLOB_PIN, DEFAULTS, signedImage,
} from './fake_skyblip.mjs';
import { SmpError } from '#skyblip/smp';
import { noticeOf } from '#skyblip/update';

import { NEWER, IMAGE_BYTES, session, asking, settled, connected, chosen } from './update_session.mjs';

const FLOWN = { aircraft_type: 7, alarm: true, alarm_volume: 1, units: 1, callsign: 'F-JABC' };

async function withSettings(options = {}) {
  const link = await connected({ stored: FLOWN, ...options });
  await link.until(state => state.settings && (state.defaults || !link.device.knowsDefaults));
  return link;
}

test('connecting reads the stored settings and the ones the firmware ships on', async () => {
  const { updater } = await withSettings();
  assert.deepEqual(updater.state.settings, FLOWN);
  assert.deepEqual(updater.state.defaults, DEFAULTS);
});

test('a save sends only what changed, is asked on the glass, and reads the device back', async () => {
  const { device, updater, until } = await withSettings();
  updater.saveSettings({ ...FLOWN, callsign: 'F-JXYZ' });
  await until(asking('set'));
  assert.deepEqual(device.staged, { callsign: 'F-JXYZ' });
  device.press();
  const saved = await until(state => state.notice?.key === 'saved' && state.settings.callsign === 'F-JXYZ');
  assert.equal(saved.phase, 'ready');
});

test('a save with nothing changed sends nothing', async () => {
  const { device, updater } = await withSettings();
  updater.saveSettings({ ...FLOWN });
  assert.equal(device.commands.includes('set'), false);
});

test('back to defaults sends the defaults the device named, and lands it on them', async () => {
  const { device, updater, until } = await withSettings();
  updater.resetSettings();
  await until(asking('set'));
  assert.deepEqual(device.staged, { aircraft_type: 1, alarm_volume: 2, units: 0, callsign: '' });
  device.press();
  const { settings } = await until(state => state.notice?.key === 'saved' && state.settings.units === 0);
  assert.deepEqual(settings, DEFAULTS);
});

test('a firmware that does not know its defaults offers no reset', async () => {
  const { device, updater } = await withSettings({ knowsDefaults: false });
  assert.equal(updater.state.defaults, null);
  updater.resetSettings();
  assert.equal(device.commands.includes('set'), false);
});

test('a save refused on the glass says so, and the settings stay as stored', async () => {
  const { device, updater, until } = await withSettings();
  updater.saveSettings({ units: 0 });
  await until(asking('set'));
  device.refuse();
  assert.equal((await until(settled)).notice.key, 'cancelled');
  assert.deepEqual(device.stored, FLOWN);
});

test('off the ground the page asks nothing at all, and landing gives it back', async () => {
  const { device, updater, until } = await chosen({ stored: FLOWN });
  device.takeOff();
  await until(state => state.status.flight === 'airborne');
  assert.equal(updater.onGround, false);
  const sent = device.commands.length;
  updater.saveSettings({ units: 0 });
  updater.recover();
  await updater.install();
  assert.equal(updater.state.notice.key, 'in_flight');
  assert.equal(device.commands.length, sent);
  device.land();
  await until(state => state.status.flight === 'ground');
  updater.saveSettings({ units: 0 });
  await until(asking('set'));
});

test('taking off while the glass asks takes the question away, and the page says why', async () => {
  const { device, updater, until } = await withSettings();
  updater.saveSettings({ units: 0 });
  await until(asking('set'));
  device.takeOff();
  assert.equal((await until(settled)).notice.key, 'in_flight');
  assert.deepEqual(device.stored, FLOWN);
});

test('the SMP refusals the page words are the ones Zephyr sends', () => {
  assert.equal(noticeOf(new SmpError(11)).key, 'window_closed');
  assert.equal(noticeOf(new SmpError(27, 1)).key, 'older');
  assert.equal(noticeOf(new SmpError(30, 1)).key, 'too_large');
  assert.equal(noticeOf(new SmpError(23, 1)).key, 'not_image');
  assert.equal(noticeOf(new SmpError(12, 1)).key, 'flash');
  assert.deepEqual(noticeOf(new SmpError(9, 1)), { key: 'smp', detail: 'SMP group 1 rc 9' });
});

const SLIM = { pin: BLOB_PIN, full: false };
const FULL = { pin: BLOB_PIN, full: true };

async function choosing(options, imu, name) {
  const link = await connected(options);
  await link.updater.choose(signedImage(NEWER, IMAGE_BYTES, { imu }), name);
  return link;
}

for (const [reported, advice] of [
  ['none', 'slim'],
  [BLOB_PIN.slice(0, 16), 'slim'],
  ['missing', 'full'],
  ['corrupt', 'full'],
  ['unreadable', 'full'],
  ['writing', 'full'],
  [null, 'full'],
]) {
  test(`a device reporting imu ${reported} is advised the ${advice} image before any file is chosen`, async () => {
    const { updater } = await connected({ imu: reported });
    assert.equal(updater.state.image.imu, reported);
    assert.equal(updater.state.advice, advice);
  });
}

test('no advice is given before the device answered the update question', () => {
  const { updater } = session();
  assert.equal(updater.state.advice, null);
});

test('a verified blob other than the one the chosen file pins turns the advice to the full image', async () => {
  const { updater } = await choosing({}, { pin: 'a'.repeat(64), full: false }, 'skyblip-go.signed.bin');
  assert.equal(updater.state.advice, 'full');
  assert.equal(updater.state.notice.key, 'needs_full');
});

for (const reported of ['missing', 'corrupt', 'unreadable', 'writing', null]) {
  test(`a slim file chosen for a device reporting imu ${reported} says it needs the full one, and still installs`, async () => {
    const { updater, until } = await choosing({ imu: reported }, SLIM, 'skyblip-go.signed.bin');
    assert.equal(updater.state.notice.key, 'needs_full');
    assert.equal(updater.state.file.full, false);
    updater.install();
    await until(asking('dfu'));
  });
}

for (const [reported, imu, name] of [
  [BLOB_PIN.slice(0, 16), SLIM, 'skyblip-go.signed.bin'],
  ['none', SLIM, 'skyblip-go.signed.bin'],
  [BLOB_PIN.slice(0, 16), FULL, 'skyblip-go.full.signed.bin'],
  ['missing', FULL, 'skyblip-go.full.signed.bin'],
]) {
  test(`${name} chosen for a device reporting imu ${reported} raises no notice`, async () => {
    const { updater } = await choosing({ imu: reported }, imu, name);
    assert.equal(updater.state.notice, null);
    assert.equal(updater.state.file.full, imu.full);
    assert.equal(updater.state.file.pin, BLOB_PIN);
  });
}

test('a device writing its blob is asked again until the blob is verified', async () => {
  const { device, updater, until } = await connected({ imu: 'writing' });
  assert.equal(updater.state.settling, true);
  device.imu = BLOB_PIN.slice(0, 16);
  updater.refresh();
  const state = await until(state => state.image.imu !== 'writing');
  assert.equal(state.settling, false);
  assert.equal(state.advice, 'slim');
});

test('a device on probation is asked again, a confirmed one is not', async () => {
  const { device, updater, until } = await connected({ image: 'probation', from: '0.1.0+12', to: '0.2.0+15' });
  assert.equal(updater.state.settling, true);
  device.image = 'confirmed';
  updater.refresh();
  assert.equal((await until(state => state.image.state === 'confirmed')).settling, false);
});

