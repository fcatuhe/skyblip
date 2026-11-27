import test from 'node:test';
import assert from 'node:assert/strict';

import {
  BLOB_PIN, DEVELOPMENT_KEY, FakeSkyblip, IMG_MGMT_ERR_CURRENT_VERSION_IS_NEWER, PRODUCTION_KEY, signedImage,
} from './fake_skyblip.mjs';
import { GATT_WRITE_BYTES } from '#skyblip/ble';
import { Updater, productOf } from '#skyblip/update';

import {
  NEWER, IMAGE_BYTES, session, READS, asking, installing, settled, connected, chosen, unexchanged,
} from './update_session.mjs';

test('connecting reads the running version, the image state and the status', async () => {
  const { updater } = await connected({ running: '0.1.0.12' });
  assert.equal(updater.state.phase, 'ready');
  assert.equal(updater.state.running, '0.1.0+12');
  assert.equal(updater.state.image.state, 'confirmed');
  assert.deepEqual(updater.state.status, {
    flight: 'ground', upload: false, batteryPercent: 80, charging: false, powerLevel: 'normal', wentDarkFlat: false, bravo: null, ownTransponder: null, noTx: null,
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

test('a chosen image newer than the running firmware is an upgrade, and the same version is not', async () => {
  const { updater } = await chosen({ running: '0.1.0.12' });
  assert.equal(updater.state.upgrade, true);
  await updater.choose(signedImage({ major: 0, minor: 1, revision: 0, build: 12 }, IMAGE_BYTES), 'skyblip-go-0.1.0+12.signed.bin');
  assert.equal(updater.state.upgrade, false);
  updater.clearImage();
  assert.equal(updater.state.upgrade, null);
});

// The page holds the device it connected, so it finds it again after the restart without the chooser.
test('after an install the device is connected again by itself once it restarts', async () => {
  const { device, updater, until } = await chosen({ returnEveryMs: 0 });
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(state => state.phase === 'rebooting');
  device.running = '0.2.0.15';
  const back = await until(state => state.phase === 'ready' && state.running);
  assert.equal(back.running, '0.2.0+15');
  assert.equal(device.connected, true);
  updater.disconnect();
});

// Brave on Linux heard nothing from a Go back from its install until the page was reloaded.
test('after an install a browser that keeps the dropped link\'s notify sessions still hears the device once it restarts', async () => {
  const { device, updater, until } = await chosen({ returnEveryMs: 0, keepsNotifySessions: true });
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(state => state.phase === 'rebooting');
  device.running = '0.2.0.15';
  const back = await until(state => state.phase === 'ready' && (state.running || state.notice));
  assert.equal(back.notice, null);
  assert.equal(back.running, '0.2.0+15');
  updater.disconnect();
});

test('a link that drops before the first SMP request says it was lost, and nothing else', async () => {
  const device = new FakeSkyblip({ bufSize: 300 }).install();
  const onConfig = device.onConfig.bind(device);
  device.onConfig = bytes => {
    onConfig(bytes);
    if (device.commands.at(-1) === 'defaults') device.drop();
  };
  const { updater, until } = session();
  await updater.connect();
  const lost = await until(state => state.phase === 'offline' && state.notice);
  assert.equal(lost.notice.key, 'link_lost');
  assert.equal(lost.notice.detail, undefined);
});

test('a device that does not come back in time is left offline, and says so', async () => {
  const { device, updater, until } = await chosen({ returnEveryMs: 0, returnWithinMs: 0 });
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(state => state.phase === 'rebooting');
  updater.bluetoothDevice.gatt.connect = () => Promise.reject(new Error('Connection attempt failed.'));
  const lost = await until(state => state.phase === 'offline');
  assert.equal(lost.notice.key, 'not_back');
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

test('a link whose MTU was never exchanged hears the status, and small_mtu for the identity, the update frame and the settings', async () => {
  const { device, updater } = await connected({ mtu: 23 });
  assert.equal(updater.state.running, '0.1.0+12');
  assert.equal(updater.state.status.flight, 'ground');
  assert.deepEqual([updater.state.identity, updater.state.image, updater.state.settings], [null, null, null]);
  assert.equal(updater.state.notice.key, 'small_mtu');
  assert.equal(device.linkDrops, 3);
  assert.equal(device.smallMtuRefusals, 3);
  assert.equal(device.refusedNotifications, 0);
});

test('an update frame pushed at link-up that does not fit is dropped unannounced', async () => {
  const { device, updater } = await connected({ mtu: 23, image: 'probation', from: '0.1.0+11', to: '0.1.0+12' });
  assert.equal(device.linkDrops, 4);
  assert.equal(device.smallMtuRefusals, 3);
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

// Each state change redraws every page open on the fleet, and a real image takes hundreds of chunks.
test('an upload reports its progress once per percent, not once per chunk', async () => {
  const { device, updater, until } = await connected();
  await updater.choose(signedImage(NEWER, 100_000), 'skyblip_go-0.2.0+15.signed.bin');
  const percents = [];
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(state => {
    if (state.phase === 'uploading') percents.push(Math.floor(100 * state.progress.sent / state.progress.total));
    return installing(state);
  });
  assert.ok(device.uploadOffsets.length > 300, `${device.uploadOffsets.length} chunks`);
  assert.deepEqual(percents, [...new Set(percents)]);
  assert.equal(percents.at(-1), 100);
});

for (const [name, arrange, reason] of [
  ['a cell below the warning refuses the upload window', { swapPowered: false }, 'low_power'],
  ['a device in flight is asked for no upload window', { flight: 'airborne' }, 'in_flight'],
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

test('a refusal on the glass reads as cancelled', async () => {
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

test('a cancel on the device during the upload reads as cancelled, not as a closed window', async () => {
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

// The unit can restart into its install before its verdict reaches the page, and a lost link then hides an install.
test('a link that drops once the last chunk is in waits for the device to come back, and says nothing was lost', async () => {
  const { device, updater, until } = await chosen({ returnEveryMs: 0 });
  device.installLanded = () => {};
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(state => state.phase === 'asking' && state.task === 'install');
  device.running = '0.2.0.15';
  device.drop(0);
  const rebooting = await until(state => state.phase !== 'asking');
  assert.equal(rebooting.phase, 'rebooting');
  assert.equal(rebooting.notice, null);
  const back = await until(state => state.phase === 'ready' && state.running);
  assert.equal(back.running, '0.2.0+15');
  updater.disconnect();
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

test('a development bootloader takes a release over a development image', async () => {
  const both = [DEVELOPMENT_KEY, PRODUCTION_KEY];
  const { device, updater, until } = await connected({ downgrade: true, key: DEVELOPMENT_KEY, bootloaderKeys: both });
  assert.deepEqual(updater.state.image.bootloaderKeys, ['A1A1A1A1', 'B2B2B2B2']);
  await updater.choose(signedImage(NEWER, IMAGE_BYTES, { key: PRODUCTION_KEY }), 'release.signed.bin');
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(state => state.phase === 'rebooting');
});

test('a development bootloader takes a development image back over a release', async () => {
  const { device, updater, until } = await connected({ key: PRODUCTION_KEY, bootloaderKeys: [DEVELOPMENT_KEY, PRODUCTION_KEY] });
  await updater.choose(signedImage(NEWER, IMAGE_BYTES, { key: DEVELOPMENT_KEY }), 'dev.signed.bin');
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(state => state.phase === 'rebooting');
});

test('a release bootloader refuses a development image before the window opens', async () => {
  const { device, updater } = await connected({ key: PRODUCTION_KEY, bootloaderKeys: [PRODUCTION_KEY] });
  await updater.choose(signedImage(NEWER, IMAGE_BYTES, { key: DEVELOPMENT_KEY }), 'dev.signed.bin');
  await updater.install();
  assert.equal(updater.state.notice.key, 'wrong_key');
  assert.equal(device.commands.includes('dfu'), false);
});

test('a unit names its product, variant, firmware, chip and tag in its identity frame', async () => {
  const { updater } = await connected({ product: 'go', variant: 'plus', bluetoothTag: 0x3f2a });
  assert.deepEqual(updater.state.identity,
    { product: 'go', variant: 'plus', version: '0.1.0+12', release: true, deviceId: '8f3a1c2e9b7d4f60', bluetoothTag: 0x3f2a });
  assert.equal(productOf(updater.state), 'go');
});

// The unit drops a reply without a word while another frame is held, so the next frame is what tells identity's answer is gone.
test('an identity answer the unit dropped is awaited no longer, and a later small_mtu is the settings\' own', async () => {
  const { updater, until } = await connected({ identity: 'dropped', mtu: 23, imu: 'none' });
  await until(state => state.notice);
  assert.equal(updater.state.notice.key, 'small_mtu');
  assert.equal(updater.state.identityRefused, null);
});

test('a unit on firmware before the identity frame names no product, and the page goes on without a notice', async () => {
  const { updater } = await connected({ identity: false });
  assert.equal(updater.state.identity, null);
  assert.equal(updater.state.notice, null);
  assert.equal(productOf(updater.state), null);
});

// The identity frame does not fit this link, so the page knows no product and the unit checks the image itself.
test('a unit whose identity never arrived refuses an image of another product itself', async () => {
  const { device, updater, until } = await connected({ mtu: 23 });
  assert.equal(productOf(updater.state), null);
  await updater.choose(signedImage(NEWER, IMAGE_BYTES, { product: 'bravo' }), 'skyblip-bravo-0.2.0+15.signed.bin');
  updater.install();
  await until(asking('dfu'));
  device.press();
  assert.equal((await until(settled)).notice.key, 'wrong_product');
  assert.equal(device.finished, false);
});

test('a Bravo refuses an image naming no product before the window opens, in words of its own', async () => {
  const { device, updater } = await connected({ product: 'bravo' });
  await updater.choose(signedImage(NEWER, IMAGE_BYTES, { product: null }), 'skyblip-bravo-dev-900.signed.bin');
  await updater.install();
  assert.equal(updater.state.notice.key, 'unmarked_image');
  assert.equal(device.commands.includes('dfu'), false);
});

test('an image for another product is refused before the window opens', async () => {
  const { device, updater } = await connected();
  await updater.choose(signedImage(NEWER, IMAGE_BYTES, { product: 'bravo' }), 'skyblip-bravo-0.2.0+15.signed.bin');
  await updater.install();
  assert.equal(updater.state.notice.key, 'wrong_product');
  assert.equal(device.commands.includes('dfu'), false);
});

test('a Bravo installs a Bravo image', async () => {
  const { device, updater, until } = await connected({ product: 'bravo' });
  await updater.choose(signedImage(NEWER, IMAGE_BYTES, { product: 'bravo' }), 'skyblip-bravo-0.2.0+15.signed.bin');
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(installing);
});

test('a Go names its product, so an image naming none is refused before the window opens', async () => {
  const { device, updater } = await connected();
  await updater.choose(signedImage(NEWER, IMAGE_BYTES, { product: null }), 'skyblip-go-0.1.0+800.signed.bin');
  await updater.install();
  assert.equal(updater.state.notice.key, 'unmarked_image');
  assert.equal(device.commands.includes('dfu'), false);
});

test('a Go on firmware before the identity frame refuses an image that names no product once its protected TLVs arrive, in words of their own', async () => {
  const { device, updater, until } = await connected({ identity: false });
  await updater.choose(signedImage(NEWER, IMAGE_BYTES, { product: null }), 'skyblip-go-0.1.0+800.signed.bin');
  updater.install();
  await until(asking('dfu'));
  device.press();
  assert.equal((await until(settled)).notice.key, 'unmarked_image');
  assert.equal(device.finished, false);
});

// The image's protected TLVs end where its 40-byte trailer starts, and the device never writes the chunk that completes them.
test('a unit refuses an image of another product before it writes the chunk that completes its class', async () => {
  const { device, updater, until } = await chosen();
  device.product = 'bravo';
  updater.install();
  await until(asking('dfu'));
  device.press();
  assert.equal((await until(settled)).notice.key, 'wrong_product');
  assert.notEqual(signedImage(NEWER, IMAGE_BYTES)[IMAGE_BYTES - 41], 0);
  assert.equal(device.slot[IMAGE_BYTES - 41], 0);
  assert.equal(device.finished, false);
});

// The base and full images of one release share its version, and the full one is how a device gets its blob.
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

// A refusal word alone cannot tell a foreign key from a slot the unit could not read back.
test('an upload refused once it landed asks the unit what it found in slot 1', async () => {
  const { device, updater, until } = await connected();
  device.push({ command: 'update', image: 'confirmed', swap_powered: true });
  await until(state => state.image.key === null);
  device.landedCheck = 'damaged';
  await updater.choose(signedImage(NEWER, IMAGE_BYTES, { key: DEVELOPMENT_KEY }), 'dev.signed.bin');
  updater.install();
  await until(asking('dfu'));
  device.press();
  const { landing, notice } = await until(state => state.landing);
  assert.equal(notice.key, 'wrong_key');
  assert.deepEqual(landing, { check: 'damaged', stagedKey: 'A1A1A1A1' });
});

test('an upload refused for low power asks nothing more', async () => {
  const { device, updater, until } = await chosen();
  device.refuseInstall = 'low_power';
  updater.install();
  await until(asking('dfu'));
  device.press();
  await until(settled);
  assert.equal(device.commands.filter(command => command === 'install_report').length, 1);
  assert.equal(updater.state.landing, null);
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
    downgrade: false, key: 'B2B2B2B2', bootloaderKeys: null,
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
  const { device, updater, until } = await chosen();
  device.claimedBy = 2;
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

