import test from 'node:test';
import assert from 'node:assert/strict';

import {
  BLOB_PIN, DEVELOPMENT_KEY, FakeSkyblip, IMG_MGMT_ERR_CURRENT_VERSION_IS_NEWER, signedImage,
} from './fake_skyblip.mjs';
import { GATT_WRITE_BYTES } from '#skyblip/ble';
import { Updater } from '#skyblip/update';

import {
  NEWER, IMAGE_BYTES, session, READS, asking, installing, settled, connected, chosen, unexchanged,
} from './update_session.mjs';

test('connecting reads the running version, the image state and the status', async () => {
  const { updater } = await connected({ running: '0.1.0.12' });
  assert.equal(updater.state.phase, 'ready');
  assert.equal(updater.state.running, '0.1.0+12');
  assert.equal(updater.state.image.state, 'confirmed');
  assert.deepEqual(updater.state.status, {
    flight: 'ground', upload: false, batteryPercent: 80, charging: false, powerLevel: 'normal', wentDarkFlat: false,
  });
});

test('the settings carry the address, and a unit not yet activated has none', async () => {
  const { updater, until } = await connected();
  const activated = await until(state => state.settings);
  assert.equal(activated.address, '00012A');
  const fresh = await connected({ address: null });
  const unactivated = await fresh.until(state => state.settings);
  assert.equal(unactivated.address, null);
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
  assert.deepEqual(device.commands.filter(command => !READS.has(command)), ['dfu']);
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

test('a link whose MTU was never exchanged hears the status, and small_mtu for the update frame and the settings', async () => {
  const { device, updater } = await connected({ mtu: 23 });
  assert.equal(updater.state.running, '0.1.0+12');
  assert.equal(updater.state.status.flight, 'ground');
  assert.deepEqual([updater.state.image, updater.state.settings], [null, null]);
  assert.equal(updater.state.notice.key, 'small_mtu');
  assert.equal(device.linkDrops, 2);
  assert.equal(device.smallMtuRefusals, 2);
  assert.equal(device.refusedNotifications, 0);
});

test('an update frame pushed at link-up that does not fit is dropped unannounced', async () => {
  const { device, updater } = await connected({ mtu: 23, image: 'probation', from: '0.1.0+11', to: '0.1.0+12' });
  assert.equal(device.linkDrops, 3);
  assert.equal(device.smallMtuRefusals, 2);
  assert.equal(updater.state.notice.key, 'small_mtu');
});

test('a link whose MTU was never exchanged still installs, asked on the glass', async () => {
  const { device, updater, until } = await unexchanged();
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(installing);
  assert.deepEqual(device.slot, signedImage(NEWER, IMAGE_BYTES));
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

test('without an echo no SMP write is longer than BLE guarantees, none overlaps, no packet overflows the buffer', async () => {
  const { device, updater, until } = await chosen({ bufSize: 300, echo: false });
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(installing);
  assert.equal(device.smp.longestAttempt, GATT_WRITE_BYTES);
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

test('a release unit refuses an older image before the window opens', async () => {
  const { device, updater } = await connected({ running: '0.2.0.16' });
  await updater.choose(signedImage(NEWER, IMAGE_BYTES), 'older.signed.bin');
  await updater.install();
  assert.equal(updater.state.notice.key, 'older');
  assert.equal(device.commands.includes('dfu'), false);
});

test('a release unit takes the version it already runs, to install it again', async () => {
  const { updater, until } = await connected({ running: '0.2.0.15' });
  await updater.choose(signedImage(NEWER, IMAGE_BYTES), 'same.signed.bin');
  updater.install();
  await until(asking('dfu'));
});

test('a development unit takes an older image all the way to the swap', async () => {
  const { device, updater, until } = await connected({ running: '0.3.0.40', downgrade: true, key: DEVELOPMENT_KEY });
  await updater.choose(signedImage(NEWER, IMAGE_BYTES, { key: DEVELOPMENT_KEY }), 'older.signed.bin');
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(state => state.phase === 'rebooting');
});

test('an image signed by another key is refused before the window opens', async () => {
  const { device, updater } = await connected();
  await updater.choose(signedImage(NEWER, IMAGE_BYTES, { key: DEVELOPMENT_KEY }), 'dev.signed.bin');
  await updater.install();
  assert.equal(updater.state.notice.key, 'wrong_key');
  assert.equal(device.commands.includes('dfu'), false);
});

// The slim and full images of one release share its version, and the full one is how a device gets its blob.
test('an image of the running version is sent', async () => {
  const { updater, until } = await connected({ running: '0.2.0.15', imu: 'missing' });
  await updater.choose(signedImage(NEWER, IMAGE_BYTES, { imu: { pin: BLOB_PIN, full: true } }), 'skyblip-go.full.signed.bin');
  updater.install();
  await until(asking('dfu'));
});

test('a device that does not name its key refuses a foreign one once it lands, in its own words', async () => {
  const { device, updater, until } = await connected();
  device.push({ command: 'update', image: 'confirmed', swap_powered: true });
  await until(state => state.image.key === null);
  await updater.choose(signedImage(NEWER, IMAGE_BYTES, { key: DEVELOPMENT_KEY }), 'dev.signed.bin');
  updater.install();
  await until(asking('dfu'));
  device.press();
  assert.equal((await until(settled)).notice.key, 'wrong_key');
  assert.equal(device.finished, true);
});

test('the device refusing an older image at the first chunk reads as older', async () => {
  const { device, updater, until } = await chosen();
  device.refuseUpload = IMG_MGMT_ERR_CURRENT_VERSION_IS_NEWER;
  updater.install();
  await until(asking('dfu'));
  device.press();
  assert.equal((await until(settled)).notice.key, 'older');
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
    downgrade: false, key: 'B2B2B2B2',
  });
});

test('an update frame arriving later replaces the one before it', async () => {
  const { device, until } = await connected({ image: 'probation', from: '0.1.0+12', to: '0.2.0+15' });
  device.push({ command: 'update', image: 'confirmed', swap_powered: true });
  const { image } = await until(state => state.image.state === 'confirmed');
  assert.equal(image.from, null);
});

test('a status frame says the cell went flat, and has no percent until the gauge reads', async () => {
  const { device, until } = await connected();
  device.push({ command: 'status', flight: 'ground', upload: false, charging: true, power_level: 'low', went_dark_flat: true });
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

