import test from 'node:test';
import assert from 'node:assert/strict';

import { FakeServer, FakeUnit, REGISTRATION_FRAME_BYTES, VECTORS, keyPair, messages, verifies } from './fake_activation.mjs';
import { ATT_HEADER_BYTES, DEFAULTS } from './fake_skyblip.mjs';
import { Activator } from '#skyblip/activation';
import { addressOf } from '#skyblip/ble';

const CHROME_MTU = 498;

function session(server) {
  const watchers = [];
  const activator = new Activator({
    server,
    onChange: state => {
      for (const watcher of watchers.splice(0)) {
        if (watcher.until(state)) watcher.resolve(state);
        else watchers.push(watcher);
      }
    },
  });
  const until = predicate =>
    new Promise(resolve => {
      if (predicate(activator.state)) resolve(activator.state);
      else watchers.push({ until: predicate, resolve });
    });
  return { activator, until };
}

async function connected(options = {}, server = new FakeServer()) {
  const unit = new FakeUnit({ mtu: CHROME_MTU, ...options }).install();
  const { activator, until } = session(server);
  await activator.connect();
  await until(state => state.read);
  return { unit, server, activator, until };
}

const confirming = state => state.phase === 'confirming';
const settled = state => state.phase === 'ready' && state.notice;

test('the vectors hold for node:crypto too, so the fakes sign as the server and the firmware do', () => {
  const server = keyPair(VECTORS.server_seed);
  const unit = keyPair(VECTORS.unit_seed);
  const fields = { deviceId: VECTORS.device_id, publicKey: VECTORS.unit_public_key, unitNonce: VECTORS.unit_nonce,
    serverNonce: VECTORS.server_nonce, serverKeyId: VECTORS.server_key_id, release: VECTORS.release, version: VECTORS.version, serial: VECTORS.serial };
  assert.equal(server.id, VECTORS.server_key_id);
  assert.equal(server.sign(messages.register(fields)), VECTORS.register_signature);
  assert.equal(unit.sign(messages.registration(fields)), VECTORS.registration_signature);
  assert.equal(server.sign(messages.unlock(fields)), VECTORS.unlock_signature);
  assert.ok(verifies(VECTORS.unit_public_key, VECTORS.registration_signature, messages.registration(fields)));
  const development = messages.registration({ ...fields, release: false });
  assert.equal(development.toString('hex'), VECTORS.development_registration_message);
  assert.equal(unit.sign(development), VECTORS.development_registration_signature);
});

test('a unit not yet activated reports no address', async () => {
  const { activator } = await connected();
  assert.equal(activator.state.phase, 'ready');
  assert.equal(activator.state.address, null);
  assert.equal(addressOf({ address: 0x00012a }), '00012A');
});

test('activating asks the server, then the glass, then stores the unlock code and reads the address back', async () => {
  const { unit, server, activator, until } = await connected();
  activator.activate();
  await until(confirming);
  assert.deepEqual(unit.commands, ['get', 'challenge', 'register']);
  unit.press();
  const done = await until(state => state.notice?.key === 'activated' && state.address);
  assert.equal(done.address, '000001');
  assert.deepEqual(server.requests, ['challenge', 'register']);
  assert.equal(unit.unlockCode.serial, 1);
  assert.deepEqual(unit.commands, ['get', 'challenge', 'register', 'unlock', 'get']);
});

test('a unit activated again keeps its serial', async () => {
  const server = new FakeServer();
  for (let round = 0; round < 2; round++) {
    const { unit, activator, until } = await connected({}, server);
    activator.activate();
    await until(confirming);
    unit.press();
    assert.equal((await until(state => state.address)).address, '000001');
  }
});

test('one press on the glass refuses, and the page names it', async () => {
  const { unit, server, activator, until } = await connected();
  activator.activate();
  await until(confirming);
  unit.refuse();
  assert.equal((await until(settled)).notice.key, 'cancelled');
  assert.deepEqual(server.requests, ['challenge']);
});

test('a unit that trusts another server key refuses the server signature as the server refuses the key', async () => {
  const { activator, until } = await connected({ trusted: keyPair('11'.repeat(32)).publicKey });
  activator.activate();
  assert.equal((await until(settled)).notice.key, 'server_unknown_server_key');
});

test('the server refusing a known unit with another key is said in its words', async () => {
  const server = new FakeServer();
  server.units.set(VECTORS.device_id, { serial: 7, publicKey: 'ab'.repeat(32) });
  const { unit, activator, until } = await connected({}, server);
  activator.activate();
  await until(confirming);
  unit.press();
  const refused = await until(settled);
  assert.equal(refused.notice.key, 'server_other_key');
  assert.equal(refused.notice.detail, `${VECTORS.device_id} ${VECTORS.unit_public_key.slice(0, 8)}`, 'what an admin finds the unit by');
  assert.equal(unit.unlockCode, null);
});

test('activating needs no ground: a unit without a serial is silent, and indoors it has no fix', async () => {
  const { unit, activator, until } = await connected({ flight: 'unknown' });
  activator.activate();
  await until(confirming);
  unit.press();
  assert.equal((await until(state => state.address)).address, '000001');
});

test('an answer that lands after the link dropped is sent to no unit, not to the next one', async () => {
  const server = new FakeServer();
  const first = await connected({}, server);
  first.activator.activate();
  await first.until(confirming);
  server.hold();
  first.unit.press();
  await first.until(state => state.phase === 'registering');
  first.unit.drop();
  await first.until(state => state.phase === 'offline');
  const second = new FakeUnit({ mtu: CHROME_MTU, deviceId: '1111111111111111' }).install();
  await first.activator.connect();
  await first.until(state => state.read);
  server.release();
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(second.commands, ['get']);
  assert.equal(first.activator.state.phase, 'ready');
  assert.equal(first.activator.state.notice, null);
});

test('a refusal from the server before the glass asks leaves the unit untouched', async () => {
  const { unit, server, activator, until } = await connected();
  server.refuseNext = 'rate_limited';
  activator.activate();
  assert.equal((await until(settled)).notice.key, 'server_rate_limited');
  assert.deepEqual(unit.commands, ['get', 'challenge']);
});

test('the unit refuses what it cannot do, and the page says why', async () => {
  const keyless = await connected({ keyed: false });
  keyless.activator.activate();
  assert.equal((await keyless.until(settled)).notice.key, 'keyless');

  const narrow = await connected({ mtu: ATT_HEADER_BYTES + REGISTRATION_FRAME_BYTES - 1 });
  narrow.activator.activate();
  assert.equal((await narrow.until(settled)).notice.key, 'small_mtu');
});

test('an iPhone link carries the registration frame', async () => {
  const { unit, activator, until } = await connected({ mtu: 185 });
  activator.activate();
  await until(confirming);
  unit.press();
  assert.equal((await until(state => state.address)).address, '000001');
});

test('a link whose MTU was never exchanged reads the unit, and is told small_mtu for the challenge reply', async () => {
  const { unit, server, activator, until } = await connected({ mtu: 23 });
  assert.equal(activator.state.address, null);
  activator.activate();
  assert.equal((await until(settled)).notice.key, 'small_mtu');
  assert.deepEqual(unit.commands, ['get', 'challenge']);
  assert.equal(unit.smallMtuRefusals, 1);
  assert.equal(unit.refusedNotifications, 0);
  assert.deepEqual(server.requests, []);
});

test('a link lost while the glass asks is said as such', async () => {
  const { unit, activator, until } = await connected();
  activator.activate();
  await until(confirming);
  unit.drop();
  const lost = await until(state => state.phase === 'offline');
  assert.equal(lost.notice.key, 'link_lost');
});

test('another app holding the unit is named', async () => {
  const { unit, activator, until } = await connected();
  unit.claimedBy = 3;
  activator.activate();
  assert.equal((await until(settled)).notice.key, 'claimed');
});

async function activated(options = {}) {
  const session = await connected(options);
  session.activator.activate();
  await session.until(confirming);
  session.unit.press();
  await session.until(state => state.offer === 'open' && state.address && state.status);
  return session;
}

test('once activated, the page offers the aircraft type, the callsign and the own ICAO the unit holds', async () => {
  const { unit, activator } = await activated({ stored: { ...DEFAULTS, aircraft_type: 4, callsign: 'F-CABC' } });
  assert.deepEqual(activator.state.settings, { aircraft_type: 4, callsign: 'F-CABC', own_icao: '' });
  assert.equal(activator.state.status.flight, 'ground');
  assert.deepEqual(unit.commands, ['get', 'challenge', 'register', 'unlock', 'get', 'status']);
});

test('saving writes the aircraft type and the callsign in one set, asked on the glass, and reads them back', async () => {
  const { unit, activator, until } = await activated();
  activator.saveSettings({ aircraft_type: 4, callsign: 'F-JXYZ', own_icao: '' });
  await until(state => state.phase === 'storing');
  assert.deepEqual(unit.staged, { aircraft_type: 4, callsign: 'F-JXYZ' });
  unit.press();
  const saved = await until(state => state.offer === 'saved' && state.settings.callsign === 'F-JXYZ');
  assert.equal(saved.notice.key, 'saved');
  assert.deepEqual(saved.settings, { aircraft_type: 4, callsign: 'F-JXYZ', own_icao: '' });
  assert.deepEqual(unit.commands.slice(-2), ['set', 'get']);
  assert.equal(unit.commands.filter(command => command === 'set').length, 1);
});

// The unit holds the transponder's address as a number, and the field shows it as its six hex digits.
test('an own ICAO typed in hex is sent as the 24-bit address, read back in hex, and cleared by 0', async () => {
  const { unit, activator, until } = await activated();
  activator.saveSettings({ aircraft_type: 1, callsign: '', own_icao: '3C6586' });
  await until(state => state.phase === 'storing');
  assert.deepEqual(unit.staged, { own_icao: 0x3C6586 });
  unit.press();
  const saved = await until(state => state.offer === 'saved' && state.settings.own_icao === '3C6586');
  assert.equal(unit.stored.own_icao, 0x3C6586);
  assert.equal(saved.phase, 'ready');

  const cleared = await activated({ stored: { ...DEFAULTS, own_icao: 0x00A0F1 } });
  assert.equal(cleared.activator.state.settings.own_icao, '00A0F1');
  cleared.activator.saveSettings({ aircraft_type: 1, callsign: '', own_icao: '' });
  await cleared.until(state => state.phase === 'storing');
  assert.deepEqual(cleared.unit.staged, { own_icao: 0 });
});

test('saving sends only what changed, and nothing when nothing did', async () => {
  const { unit, activator, until } = await activated();
  activator.saveSettings({ aircraft_type: 1, callsign: '', own_icao: '' });
  assert.ok(!unit.commands.includes('set'));
  activator.saveSettings({ aircraft_type: 1, callsign: 'F-JXYZ', own_icao: '' });
  await until(state => state.phase === 'storing');
  assert.deepEqual(unit.staged, { callsign: 'F-JXYZ' });
});

test('skipping leaves the unit on its defaults and sends nothing', async () => {
  const { unit, activator } = await activated();
  activator.skipSettings();
  assert.equal(activator.state.offer, 'skipped');
  assert.equal(activator.state.notice.key, 'skipped');
  activator.saveSettings({ aircraft_type: 4, callsign: 'F-JXYZ' });
  assert.ok(!unit.commands.includes('set'));
});

test('a unit in flight refuses the settings, and keeps the offer open until it lands', async () => {
  const { unit, activator, until } = await activated({ flight: 'airborne' });
  assert.equal(activator.state.status.flight, 'airborne');
  activator.saveSettings({ aircraft_type: 4, callsign: 'F-JXYZ' });
  const refused = await until(settled);
  assert.deepEqual(refused.notice, { key: 'in_flight', during: 'set' });
  assert.equal(refused.offer, 'open');

  unit.land();
  await until(state => state.status.flight === 'ground');
  activator.saveSettings({ aircraft_type: 4, callsign: 'F-JXYZ' });
  await until(state => state.phase === 'storing');
  unit.press();
  assert.equal((await until(state => state.offer === 'saved')).notice.key, 'saved');
});

test('a unit with no fix since boot saves the settings indoors', async () => {
  const { unit, activator, until } = await activated({ flight: 'unknown' });
  activator.saveSettings({ aircraft_type: 4, callsign: 'F-JXYZ' });
  await until(state => state.phase === 'storing');
  unit.press();
  const saved = await until(state => state.offer === 'saved' && state.settings.callsign === 'F-JXYZ');
  assert.equal(saved.notice.key, 'saved');
  assert.equal(saved.status.flight, 'unknown');
});

test('a refusal of the settings keeps the offer open, and is named as the settings', async () => {
  const { unit, activator, until } = await activated();
  activator.saveSettings({ aircraft_type: 4, callsign: 'F-JXYZ' });
  await until(state => state.phase === 'storing');
  unit.refuse();
  const refused = await until(settled);
  assert.deepEqual(refused.notice, { key: 'cancelled', during: 'set' });
  assert.equal(refused.offer, 'open');

  activator.saveSettings({ aircraft_type: 4, callsign: 'F-JXYZ' });
  assert.deepEqual((await until(state => state.notice?.key === 'refused_on_device')).notice, { key: 'refused_on_device', during: 'set' });
});

test('a cell too low for the settings refuses them, named as the settings', async () => {
  const { unit, activator, until } = await activated();
  unit.swapPowered = false;
  activator.saveSettings({ aircraft_type: 4, callsign: 'F-JXYZ' });
  assert.deepEqual((await until(state => state.notice?.key === 'low_power')).notice, { key: 'low_power', during: 'set' });
});

test('a link lost while the glass asks for the settings is said as such', async () => {
  const { unit, activator, until } = await activated();
  activator.saveSettings({ aircraft_type: 4, callsign: 'F-JXYZ' });
  await until(state => state.phase === 'storing');
  unit.drop();
  const lost = await until(state => state.phase === 'offline');
  assert.equal(lost.notice.key, 'link_lost');
  assert.equal(lost.offer, null);
});

test('a link lost while the page reads the unit back after the unlock is said as such, and nothing more is written', async () => {
  const { unit, activator, until } = await connected();
  activator.activate();
  await until(confirming);
  unit.press();
  await until(state => state.offer === 'open');
  unit.drop();
  const lost = await until(state => state.phase === 'offline');
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(activator.state.phase, 'offline');
  assert.equal(activator.state.offer, null);
  assert.deepEqual(activator.state.notice, { key: 'link_lost' });
  assert.equal(lost.notice.key, 'link_lost');
  assert.deepEqual(unit.commands, ['get', 'challenge', 'register', 'unlock']);
});

test('skipping keeps the settings the unit already holds', async () => {
  const { unit, activator } = await activated({ stored: { ...DEFAULTS, aircraft_type: 4, callsign: 'F-CABC' } });
  activator.skipSettings();
  assert.equal(activator.state.notice.key, 'skipped');
  assert.ok(!unit.commands.includes('set'));
  assert.equal(unit.stored.aircraft_type, 4);
  assert.equal(unit.stored.callsign, 'F-CABC');
});
