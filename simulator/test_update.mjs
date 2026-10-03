import test from 'node:test';
import assert from 'node:assert/strict';

import { BLOB_PIN, DEFAULTS, FakeSkyblip, IMG_MGMT_ERR_CURRENT_VERSION_IS_NEWER, signedImage } from './fake_skyblip.mjs';
import { GATT_WRITE_BYTES } from './ble.js';
import { SmpError } from './smp.js';
import { Updater, noticeOf } from './update.js';

const NEWER = { major: 0, minor: 2, revision: 0, build: 15 };
const IMAGE_BYTES = 6000;

function session() {
  const watchers = [];
  const updater = new Updater({
    onChange: state => {
      for (const watcher of watchers.splice(0)) {
        if (watcher.until(state)) watcher.resolve(state);
        else watchers.push(watcher);
      }
    },
  });
  const until = predicate =>
    new Promise(resolve => {
      if (predicate(updater.state)) resolve(updater.state);
      else watchers.push({ until: predicate, resolve });
    });
  return { updater, until };
}

const READS = new Set(['update', 'status', 'get', 'defaults']);
const asking = task => state => state.phase === 'confirming' && state.task === task;
const installing = state => state.phase === 'installing';
const settled = state => state.phase === 'ready' && state.notice;

async function connected(options = {}) {
  const device = new FakeSkyblip({ bufSize: 300, ...options }).install();
  const { updater, until } = session();
  await updater.connect();
  await until(state => state.status && state.running);
  return { device, updater, until };
}

async function chosen(options) {
  const link = await connected(options);
  await link.updater.choose(signedImage(NEWER, IMAGE_BYTES), 'skyblip_go-0.2.0+15.signed.bin');
  return link;
}

test('connecting reads the running version, the image state and the status', async () => {
  const { updater } = await connected({ running: '0.1.0.12' });
  assert.equal(updater.state.phase, 'ready');
  assert.equal(updater.state.running, '0.1.0+12');
  assert.equal(updater.state.image.state, 'confirmed');
  assert.deepEqual(updater.state.status, {
    flight: 'ground', upload: false, batteryPercent: 80, charging: false, powerLevel: 'OK', wentDarkFlat: false,
  });
});

test('an install asks once on the glass, naming the version, and installs once the image lands', async () => {
  const { device, updater, until } = await chosen();
  updater.install();
  await until(asking('dfu'));
  assert.equal(device.approved, '0.2.0+15');
  device.press();
  const uploading = await until(state => state.phase === 'uploading');
  assert.equal(uploading.progress.total, IMAGE_BYTES);
  await until(installing);
  assert.deepEqual(device.slot, signedImage(NEWER, IMAGE_BYTES));
  assert.equal(device.finished, true);
  const after = await until(state => state.phase === 'rebooting');
  assert.equal(after.notice, null);
  assert.equal(after.running, null, 'the version that ran before the install is not shown as running');
  assert.deepEqual(device.commands.filter(cmd => !READS.has(cmd)), ['dfu']);
});

test('a verdict that lands before the reply to the last chunk still reads as installing', async () => {
  const { device, updater, until } = await chosen({ verdictFirst: true });
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(installing);
  assert.equal((await until(state => state.phase === 'rebooting')).notice, null);
});

async function uploaded(options) {
  const link = await chosen(options);
  link.updater.install();
  await link.until(asking('dfu'));
  link.device.press();
  await link.until(installing);
  assert.deepEqual(link.device.slot, signedImage(NEWER, IMAGE_BYTES));
  return link;
}

test('at the MTU Chrome settles on with this device, every upload request is one write of 495 bytes', async () => {
  const { device } = await uploaded({ bufSize: 2475, mtu: 498 });
  assert.equal(device.longestAttempt, 495);
  assert.ok(device.uploadWrites.every(writes => writes === 1), `writes per request: ${device.uploadWrites}`);
});

test('at an iPhone MTU the requests shrink to the 182 bytes it carries, and nothing is cut short', async () => {
  const { device } = await uploaded({ bufSize: 2475, mtu: 185 });
  assert.equal(device.longestAttempt, 182);
  assert.ok(device.uploadWrites.every(writes => writes === 1));
});

test('a link whose MTU was never exchanged sends the whole buffer in 20-byte slices', async () => {
  const { device } = await uploaded({ bufSize: 2475, mtu: 23 });
  assert.equal(device.longestAttempt, 20);
  assert.ok(device.longestPacket > 2000, `packet of ${device.longestPacket}`);
});

test('a device that does not echo is written in 20-byte slices', async () => {
  const { device } = await uploaded({ bufSize: 2475, mtu: 498, echo: false });
  assert.equal(device.smp.longestAttempt, 20);
});

test('a write the browser refuses steps down to a smaller one, and the upload carries on', async () => {
  const { device } = await uploaded({ bufSize: 2475, mtu: 498, writeCeiling: 244, oversize: 'reject' });
  assert.equal(device.longestWrite, 244);
  assert.equal(device.overlaps, 0);
});

test('no GATT write is longer than BLE guarantees, none overlaps, no packet overflows the buffer', async () => {
  const { device, updater, until } = await chosen({ bufSize: 300, mtu: 23 });
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(installing);
  assert.equal(device.longestWrite, GATT_WRITE_BYTES);
  assert.ok(device.longestPacket <= 300, `packet of ${device.longestPacket}`);
  assert.equal(device.overlaps, 0);
});

test('progress only moves forward and ends at the image size', async () => {
  const { device, updater, until } = await chosen();
  const seen = [];
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(state => {
    if (state.progress) seen.push(state.progress.sent);
    return installing(state);
  });
  assert.deepEqual(seen, [...seen].sort((a, b) => a - b));
  assert.equal(seen.at(-1), IMAGE_BYTES);
});

for (const [name, arrange, reason] of [
  ['a cell below the warning refuses the upload window', { swapPowered: false }, 'low_power'],
  ['a device not proven on the ground refuses the upload window', { onGround: false }, 'in_flight'],
]) {
  test(`${name}, and the page says ${reason}`, async () => {
    const { updater, until } = await chosen(arrange);
    updater.install();
    const state = await until(settled);
    assert.equal(state.notice.key, reason);
  });
}

test('a question nobody answered reads as expired, and the next install asks again', async () => {
  const { device, updater, until } = await chosen();
  updater.install();
  await until(asking('dfu'));
  device.expire();
  assert.equal((await until(settled)).notice.key, 'expired');
  updater.install();
  await until(asking('dfu'));
});

test('one press on the glass reads as cancelled', async () => {
  const { device, updater, until } = await chosen();
  updater.install();
  await until(asking('dfu'));
  device.refuse();
  assert.equal((await until(settled)).notice.key, 'cancelled');
});

for (const reason of ['not_approved', 'nothing_staged', 'low_power', 'in_flight']) {
  test(`an upload the device will not install as ${reason} is said, and the next install asks again`, async () => {
    const { device, updater, until } = await chosen();
    device.refuseInstall = reason;
    updater.install();
    await until(asking('dfu'));
    device.press();
    assert.equal((await until(settled)).notice.key, reason);
    device.refuseInstall = null;
    updater.install();
    await until(asking('dfu'));
    device.press();
    await until(installing);
  });
}

test('an image other than the one the glass named is not installed', async () => {
  const { device, updater, until } = await chosen();
  updater.install();
  await until(asking('dfu'));
  device.approved = '0.3.0+1';
  device.press();
  assert.equal((await until(settled)).notice.key, 'not_approved');
});

test('one press on the glass during the upload reads as cancelled, not as a closed window', async () => {
  const { device, updater, until } = await chosen();
  device.refuseAfterPackets = 3;
  updater.install();
  await until(asking('dfu'));
  device.press();
  assert.equal((await until(settled)).notice.key, 'cancelled');
  await new Promise(resolve => setImmediate(() => setImmediate(() => setImmediate(resolve))));
  assert.equal(updater.state.notice.key, 'cancelled');
  assert.equal(device.finished, false);
});

test('the window closing mid-upload says so, and the next window resumes at the device offset', async () => {
  const { device, updater, until } = await chosen();
  device.closeWindowAfterPackets = 5;
  updater.install();
  await until(asking('dfu'));
  device.press();
  const stopped = await until(settled);
  assert.equal(stopped.notice.key, 'window_closed');
  const reached = stopped.progress.sent;
  assert.ok(reached > 0);
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(installing);
  const resumed = device.uploadOffsets.slice(device.uploadOffsets.lastIndexOf(0) + 1);
  assert.equal(resumed[0], reached);
  assert.deepEqual(device.slot, signedImage(NEWER, IMAGE_BYTES));
});

test('a link that drops mid-upload says so', async () => {
  const { device, updater, until } = await chosen();
  device.dropAfterPackets = 3;
  updater.install();
  await until(asking('dfu'));
  device.press();
  const state = await until(state => state.phase === 'offline');
  assert.equal(state.notice.key, 'link_lost');
});

test('a disconnect asked for is not reported as a lost link', async () => {
  const { updater, until } = await connected();
  updater.disconnect();
  const state = await until(state => state.phase === 'offline');
  assert.equal(state.notice, null);
});

test('an image older than the running one is refused before the window opens', async () => {
  const { device, updater } = await connected({ running: '0.3.0.2' });
  await updater.choose(signedImage(NEWER, IMAGE_BYTES), 'older.signed.bin');
  await updater.install();
  assert.equal(updater.state.notice.key, 'not_newer');
  assert.equal(device.commands.includes('dfu'), false);
});

// The slim and full images of one release share its version, and the full one is how a device gets its blob.
test('an image of the running version is sent', async () => {
  const { updater, until } = await connected({ running: '0.2.0.15', imu: 'missing' });
  await updater.choose(signedImage(NEWER, IMAGE_BYTES, { pin: BLOB_PIN, full: true }), 'skyblip-go.full.signed.bin');
  updater.install();
  await until(asking('dfu'));
});

test('the device refusing a downgrade reads as not newer too', async () => {
  const { device, updater, until } = await chosen();
  device.refuseUpload = IMG_MGMT_ERR_CURRENT_VERSION_IS_NEWER;
  updater.install();
  await until(asking('dfu'));
  device.press();
  assert.equal((await until(settled)).notice.key, 'not_newer');
});

test('a file that is not an MCUboot image is refused before anything is sent', async () => {
  const { device, updater } = await connected();
  await updater.choose(new TextEncoder().encode('UF2\nWQ]\x9e not a signed image at all'), 'skyblip.uf2');
  assert.equal(updater.state.notice.key, 'not_image');
  assert.equal(updater.state.file, null);
  await updater.install();
  assert.equal(updater.state.notice.key, 'no_file');
  assert.equal(device.commands.includes('dfu'), false);
});

test('an update frame pushed on connect is read without being asked', async () => {
  const device = new FakeSkyblip({ image: 'reverted', from: '0.1.0+12', to: '0.2.0+15', settings: 'prior' }).install();
  device.onConfig = () => {};
  const { updater, until } = session();
  await updater.connect();
  const { image } = await until(state => state.image);
  assert.deepEqual(image, {
    state: 'reverted', from: '0.1.0+12', to: '0.2.0+15', settings: 'prior', swapPowered: true, imu: BLOB_PIN.slice(0, 16),
  });
});

test('an update frame arriving later replaces the one before it', async () => {
  const { device, until } = await connected({ image: 'probation', from: '0.1.0+12', to: '0.2.0+15' });
  device.push({ cmd: 'update', image: 'confirmed', swap_powered: true });
  const { image } = await until(state => state.image.state === 'confirmed');
  assert.equal(image.from, null);
});

test('a status frame from before #87 hides a gauge that has no divider', async () => {
  const { device, until } = await connected();
  device.push({ cmd: 'status', reset: 'POWER ON', flight: 'ground', upload: false, battery_percent: 0, battery_valid: false, charging: false, power_level: '--' });
  const { status } = await until(state => state.status.powerLevel === '--');
  assert.equal(status.batteryPercent, null);
});

test('a status frame from #87 says the cell went flat, and has no percent until the gauge reads', async () => {
  const { device, until } = await connected();
  device.push({ cmd: 'status', flight: 'ground', upload: false, charging: true, power_level: 'LOW', went_dark_flat: true });
  const { status } = await until(state => state.status.wentDarkFlat);
  assert.equal(status.batteryPercent, null);
  assert.equal(status.charging, true);
});

test('another app holding the config link is named, not waited on', async () => {
  const { updater, until } = await chosen();
  const device = new FakeSkyblip({ claimedBy: 2 }).install();
  await updater.connect();
  updater.install();
  assert.equal((await until(state => state.notice)).notice.key, 'claimed');
  assert.ok(device.commands.length > 0);
});

test('recovery is asked on the glass, and the link it drops is expected', async () => {
  const { device, updater, until } = await connected();
  updater.recover();
  await until(asking('recovery'));
  device.press();
  await until(state => state.phase === 'recovering');
  await new Promise(resolve => setImmediate(() => setImmediate(() => setImmediate(resolve))));
  assert.equal(device.connected, false);
  assert.equal(updater.state.phase, 'recovering');
  assert.equal(updater.state.notice, null);
});

test('a device without the SMP service is not taken for a skyBlip', async () => {
  new FakeSkyblip({ smp: false }).install();
  const { updater, until } = session();
  await updater.connect();
  const state = await until(state => state.phase === 'offline' && state.notice);
  assert.equal(state.notice.key, 'not_skyblip');
});

test('a browser with no Web Bluetooth says so before any picker', async () => {
  Object.defineProperty(globalThis, 'navigator', { value: {}, configurable: true, writable: true });
  const { updater } = session();
  await updater.connect();
  assert.equal(updater.state.phase, 'offline');
  assert.equal(updater.state.notice.key, 'no_bluetooth');
});

const FLOWN = { aircraft_type: 7, alarm: true, alarm_volume: 5, units: 1, callsign: 'F-JABC' };

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
  assert.deepEqual(device.staged, { aircraft_type: 1, alarm_volume: 3, units: 0, callsign: '' });
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
  assert.equal(noticeOf(new SmpError(27, 1)).key, 'not_newer');
  assert.equal(noticeOf(new SmpError(30, 1)).key, 'too_large');
  assert.equal(noticeOf(new SmpError(23, 1)).key, 'not_image');
  assert.equal(noticeOf(new SmpError(12, 1)).key, 'flash');
  assert.deepEqual(noticeOf(new SmpError(9, 1)), { key: 'smp', detail: 'SMP group 1 rc 9' });
});

const SLIM = { pin: BLOB_PIN, full: false };
const FULL = { pin: BLOB_PIN, full: true };

async function choosing(options, imu, name) {
  const link = await connected(options);
  await link.updater.choose(signedImage(NEWER, IMAGE_BYTES, imu), name);
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

