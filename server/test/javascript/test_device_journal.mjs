import test from 'node:test';
import assert from 'node:assert/strict';

import { FakeSkyblip, signedImage } from './fake_skyblip.mjs';
import { ActivationWitness } from '#skyblip/device_journal';
import { Fleet } from '#skyblip/fleet';

function airwaves(...fakes) {
  const devices = new Map(fakes.map(fake => [fake, fake.device()]));
  const radio = {
    requestDevice: async ({ filters }) => {
      const fake = fakes.find(each => filters.some(({ name, services }) => (name && each.name === name) || services));
      if (!fake) throw Object.assign(new Error('User cancelled the requestDevice() chooser.'), { name: 'NotFoundError' });
      return devices.get(fake);
    },
    getDevices: null,
  };
  Object.defineProperty(globalThis, 'navigator', { value: { bluetooth: radio }, configurable: true, writable: true });
  return radio;
}

function recorder() {
  const entries = [];
  return { entries, record: (name, properties, { device } = {}) => entries.push({ name, device, ...properties }), named: name => entries.filter(entry => entry.name === name) };
}

const settled = (fleet, predicate) => new Promise(resolve => {
  if (predicate(fleet)) return resolve(fleet);
  const stop = fleet.listen(() => {
    if (predicate(fleet)) {
      stop();
      resolve(fleet);
    }
  });
});

test('an upload the unit refused once it landed is told with what it found in slot 1 and both keys', async () => {
  const fake = new FakeSkyblip({ name: 'skyBlip Go 00012A', bufSize: 300, installReport: {} });
  airwaves(fake);
  const journal = recorder();
  const fleet = new Fleet({ journal });
  const unit = await fleet.connectNew();
  await settled(fleet, () => unit.state.running);
  await unit.updater.choose(signedImage({ major: 0, minor: 2, revision: 0, build: 15 }, 6000), 'skyblip-go-0.2.0+15.signed.bin');
  fake.refuseInstall = 'wrong_key';
  unit.updater.install();
  await settled(fleet, () => unit.state.phase === 'confirming');
  fake.press();
  await settled(fleet, () => journal.named('device.upload_refused').length);
  unit.disconnect();

  const [started] = journal.named('device.task_started');
  assert.equal(started.key, 'B2B2B2B2');
  assert.match(started.sha, /^[0-9a-f]{16}$/);
  assert.equal(journal.named('device.read')[0].key, 'B2B2B2B2');
  const [refused] = journal.named('device.upload_refused');
  assert.deepEqual(
    { reason: refused.reason, version: refused.version, check: refused.check, staged_key: refused.staged_key, key: refused.key, unit_key: refused.unit_key },
    { reason: 'wrong_key', version: '0.2.0+15', check: 'matched', staged_key: 'B2B2B2B2', key: 'B2B2B2B2', unit_key: 'B2B2B2B2' },
  );
});

// A pilot who asks for help is read back from the journal, so a connection says what the unit was like.
test('a connection is told once, with what the device runs and holds', async () => {
  airwaves(new FakeSkyblip({ name: 'skyBlip Go 00012A', bufSize: 300, settings: 'prior' }));
  const journal = recorder();
  const fleet = new Fleet({ journal });
  const unit = await fleet.connectNew();
  await settled(fleet, () => unit.state.running && journal.named('device.read').length);
  unit.disconnect();
  await settled(fleet, () => journal.named('device.disconnected').length);

  const [connected] = journal.named('device.connected');
  assert.equal(connected.device, 'go-00012a');
  assert.equal(connected.returned, false);
  assert.equal(typeof connected.connect_ms, 'number');
  const [read] = journal.named('device.read');
  assert.deepEqual(
    { product: read.product, running: read.running, image: read.image, track: read.track, imu: read.imu, settings: read.settings, activated: read.activated, flight: read.flight },
    { product: 'skyblip_go', running: '0.1.0+12', image: 'confirmed', track: 'release', imu: 'held', settings: 'prior', activated: true, flight: 'ground' },
  );
  const [disconnected] = journal.named('device.disconnected');
  assert.equal(disconnected.expected, false);
  assert.equal(journal.named('device.read').length, 1);
});

test('a chooser closed with no pick is told, with the browser\'s words for why', async () => {
  airwaves();
  const journal = recorder();
  const fleet = new Fleet({ journal });
  assert.equal(await fleet.connectNew(), null);
  assert.deepEqual(journal.entries, [{ name: 'bluetooth.chooser_closed', device: null, filter: 'new', error: 'NotFoundError', detail: 'User cancelled the requestDevice() chooser.' }]);
});

test('an install is told from the question to the unit back on the new version', async () => {
  const fake = new FakeSkyblip({ name: 'skyBlip Go 00012A', bufSize: 300 });
  airwaves(fake);
  const journal = recorder();
  const fleet = new Fleet({ journal, returnEveryMs: 0 });
  const unit = await fleet.connectNew();
  await settled(fleet, () => unit.state.running);
  await unit.updater.choose(signedImage({ major: 0, minor: 2, revision: 0, build: 15 }, 6000), 'skyblip-go-0.2.0+15.signed.bin');
  unit.attempted = '0.2.0+15';
  unit.updater.install();
  await settled(fleet, () => unit.state.phase === 'confirming');
  fake.press();
  await settled(fleet, () => unit.state.phase === 'rebooting');
  fake.running = '0.2.0.15';
  await settled(fleet, () => journal.named('device.installed').length);
  unit.disconnect();

  assert.deepEqual(journal.named('device.task_started').map(({ task, version, upgrade }) => ({ task, version, upgrade })), [{ task: 'install', version: '0.2.0+15', upgrade: true }]);
  const [ended] = journal.named('device.task_ended');
  assert.equal(ended.outcome, 'granted');
  assert.equal(ended.confirmed, true);
  assert.equal(ended.sent_bytes, ended.total_bytes);
  const [restarted] = journal.named('device.disconnected');
  assert.deepEqual({ phase: restarted.phase, expected: restarted.expected }, { phase: 'installing', expected: true });
  assert.deepEqual(journal.named('device.connected').map(({ returned }) => returned), [false, true]);
  const [installed] = journal.named('device.installed');
  assert.deepEqual({ from: installed.from, to: installed.to }, { from: '0.1.0+12', to: '0.2.0+15' });
});

test('a refusal on the glass is told as the notice the pilot read and the task it ended', async () => {
  const fake = new FakeSkyblip({ name: 'skyBlip Go 00012A', bufSize: 300 });
  airwaves(fake);
  const journal = recorder();
  const fleet = new Fleet({ journal });
  const unit = await fleet.connectNew();
  await settled(fleet, () => unit.state.running);
  await unit.updater.choose(signedImage({ major: 0, minor: 2, revision: 0, build: 15 }, 6000), 'skyblip-go-0.2.0+15.signed.bin');
  unit.updater.install();
  await settled(fleet, () => unit.state.phase === 'confirming');
  fake.refuse();
  await settled(fleet, () => journal.named('device.task_ended').length);
  unit.disconnect();

  const [noticed] = journal.named('device.noticed');
  assert.deepEqual({ key: noticed.key, task: noticed.task, phase: noticed.phase }, { key: 'cancelled', task: 'install', phase: 'ready' });
  const [ended] = journal.named('device.task_ended');
  assert.deepEqual({ task: ended.task, outcome: ended.outcome, confirmed: ended.confirmed }, { task: 'install', outcome: 'cancelled', confirmed: true });
});

// The line pilots used to copy by hand now reaches the server by itself.
test('a unit that reverted an install is told with its install report', async () => {
  const installReport = { check: 'matched', swap: 'test', request_error: 0, boots: 2, probation_boots: 1, proof: 'started', reset: 'watchdog' };
  airwaves(new FakeSkyblip({ name: 'skyBlip Go 00012A', bufSize: 300, image: 'reverted', from: '0.1.0+12', to: '0.2.0+15', installReport }));
  const journal = recorder();
  const fleet = new Fleet({ journal });
  const unit = await fleet.connectNew();
  await settled(fleet, () => journal.named('device.install_reported').length);
  unit.disconnect();

  const [reported] = journal.named('device.install_reported');
  assert.deepEqual(
    { cause: reported.cause, image: reported.image, to: reported.to, report: reported.report, boots: reported.boots, probation_boots: reported.probation_boots, reset: reported.reset },
    { cause: 'probation_failed', image: 'reverted', to: '0.2.0+15', report: true, boots: 2, probation_boots: 1, reset: 'watchdog' },
  );
});

test('an activation is told step by step, with what refused it', () => {
  let now = 0;
  const witness = new ActivationWitness({ now: () => now });
  assert.deepEqual(witness.witness({ phase: 'connecting', notice: null }), [['activation.stepped', { phase: 'connecting', previous: undefined, lasted_ms: null }]]);
  now = 1200;
  assert.deepEqual(witness.witness({ phase: 'ready', notice: null }), [['activation.stepped', { phase: 'ready', previous: 'connecting', lasted_ms: 1200 }]]);
  assert.deepEqual(witness.witness({ phase: 'ready', notice: { key: 'server_daily_cap' } }), [['activation.noticed', { key: 'server_daily_cap', detail: undefined, phase: 'ready' }]]);
  assert.deepEqual(witness.witness({ phase: 'ready', notice: { key: 'server_daily_cap' } }), []);
});
