import test from 'node:test';
import assert from 'node:assert/strict';

import { FakeSkyblip, IMG_MGMT_ERR_CURRENT_VERSION_IS_NEWER, signedImage } from './fake_skyblip.mjs';
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

const asking = task => state => state.phase === 'confirming' && state.task === task;
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

test('an install asks twice on the glass and lands the whole image in the slot', async () => {
  const { device, updater, until } = await chosen();
  updater.install();
  await until(asking('dfu'));
  device.press();
  const uploading = await until(state => state.phase === 'uploading');
  assert.equal(uploading.progress.total, IMAGE_BYTES);
  await until(asking('apply'));
  assert.deepEqual(device.slot, signedImage(NEWER, IMAGE_BYTES));
  assert.equal(device.finished, true);
  device.press();
  await until(state => state.phase === 'installing');
  const after = await until(state => state.phase === 'rebooting');
  assert.equal(after.notice, null);
  assert.equal(after.running, null, 'the version that ran before the install is not shown as running');
  assert.deepEqual(device.commands.filter(cmd => cmd !== 'update' && cmd !== 'status'), ['dfu', 'apply']);
});

test('no GATT write is longer than BLE guarantees, none overlaps, no packet overflows the buffer', async () => {
  const { device, updater, until } = await chosen({ bufSize: 300, mtu: 23 });
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(asking('apply'));
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
    return state.phase === 'confirming' && state.task === 'apply';
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

for (const reason of ['nothing_staged', 'upload_unfinished']) {
  test(`an install refused as ${reason} sends the whole image again next time`, async () => {
    const { device, updater, until } = await chosen();
    updater.install();
    await until(asking('dfu'));
    device.refuseApply = reason;
    device.press();
    assert.equal((await until(settled)).notice.key, reason);
    device.refuseApply = null;
    const packets = device.uploadOffsets.length;
    updater.install();
    await until(asking('apply'));
    assert.equal(device.uploadOffsets[packets], 0);
    assert.equal(device.finished, true);
  });
}

// #89 refuses at the confirmation too: a second upload can start while INSTALL stands on the glass.
test('an install refused at the press, after the prompt, is still said', async () => {
  const { device, updater, until } = await chosen();
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(asking('apply'));
  device.finished = false;
  device.press();
  assert.equal((await until(settled)).notice.key, 'upload_unfinished');
});

test('an apply prompt that expired is asked again without uploading again', async () => {
  const { device, updater, until } = await chosen();
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(asking('apply'));
  const packets = device.uploadOffsets.length;
  device.expire();
  await until(settled);
  updater.install();
  await until(asking('apply'));
  assert.equal(device.uploadOffsets.length, packets);
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
  await until(asking('apply'));
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

test('an image no newer than the running one is refused before the window opens', async () => {
  const { device, updater } = await connected({ running: '0.2.0.15' });
  await updater.choose(signedImage(NEWER, IMAGE_BYTES), 'same.signed.bin');
  await updater.install();
  assert.equal(updater.state.notice.key, 'not_newer');
  assert.equal(device.commands.includes('dfu'), false);
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
  assert.deepEqual(image, { state: 'reverted', from: '0.1.0+12', to: '0.2.0+15', settings: 'prior', swapPowered: true });
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

test('the SMP refusals the page words are the ones Zephyr sends', () => {
  assert.equal(noticeOf(new SmpError(11)).key, 'window_closed');
  assert.equal(noticeOf(new SmpError(27, 1)).key, 'not_newer');
  assert.equal(noticeOf(new SmpError(30, 1)).key, 'too_large');
  assert.equal(noticeOf(new SmpError(23, 1)).key, 'not_image');
  assert.equal(noticeOf(new SmpError(12, 1)).key, 'flash');
  assert.deepEqual(noticeOf(new SmpError(9, 1)), { key: 'smp', detail: 'SMP group 1 rc 9' });
});
