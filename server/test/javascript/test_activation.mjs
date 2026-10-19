import test from 'node:test';
import assert from 'node:assert/strict';

import { FakeServer, FakeUnit, VECTORS, keyPair, messages, verifies } from './fake_activation.mjs';
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
});

test('a unit not yet activated reports no address', async () => {
  const { activator } = await connected();
  assert.equal(activator.state.phase, 'ready');
  assert.equal(activator.state.address, null);
  assert.equal(addressOf({ address: 0x00012a }), '00012A');
  assert.equal(addressOf({ addr: 0x5b5afe }), '5B5AFE', 'firmware before #126 says addr');
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
  const { unit, activator, until } = await connected({ onGround: false });
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

  const narrow = await connected({ mtu: 185 });
  narrow.activator.activate();
  assert.equal((await narrow.until(settled)).notice.key, 'small_mtu');
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
