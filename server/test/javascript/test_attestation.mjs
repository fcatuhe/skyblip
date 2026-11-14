import test from 'node:test';
import assert from 'node:assert/strict';

import { ATTESTATION_FRAME_BYTES, FakeServer, FakeUnit, VECTORS, keyPair, messages } from './fake_activation.mjs';
import { ATT_HEADER_BYTES, signedImage } from './fake_skyblip.mjs';
import { NEWER, asking, session } from './update_session.mjs';

const CHROME_MTU = 498;
const ADDRESS = 0x00012a;

function knowing(server) {
  server.units.set(VECTORS.device_id, { serial: ADDRESS, publicKey: VECTORS.unit_public_key });
  return server;
}

async function connected({ server = knowing(new FakeServer()), ...options } = {}) {
  const unit = new FakeUnit({ mtu: CHROME_MTU, address: ADDRESS, ...options }).install();
  const { updater, until } = session({ attestations: server.attestations });
  await updater.connect();
  await until(state => state.status && state.running);
  return { unit, server, updater, until };
}

// INFO: fc 14nov26 the fakes answer on setImmediate, so a few turns let an exchange that needs no press run out
async function turns(count = 20) {
  for (let turn = 0; turn < count; turn++) await new Promise(resolve => setImmediate(resolve));
}

test('node:crypto signs the attestation as the vectors, which the firmware and the server check', () => {
  const fields = { deviceId: VECTORS.device_id, publicKey: VECTORS.unit_public_key, serverNonce: VECTORS.server_nonce, release: VECTORS.release, version: VECTORS.version };
  assert.equal(messages.attestation(fields).toString('hex'), VECTORS.attestation_message);
  assert.equal(keyPair(VECTORS.unit_seed).sign(messages.attestation(fields)), VECTORS.attestation_signature);
});

test('an activated unit states its firmware to the server on every connection, with no press', async () => {
  const { unit, server, updater } = await connected();
  await turns();
  assert.equal(unit.commands.at(-1), 'attest');
  assert.deepEqual(server.firmwares, [{ device_id: VECTORS.device_id, version: VECTORS.version, release: VECTORS.release }]);
  assert.equal(updater.state.phase, 'ready');
  assert.equal(updater.state.notice, null);

  updater.disconnect();
  await updater.connect();
  await turns();
  assert.equal(server.firmwares.length, 2);
});

test('a unit not yet activated states nothing, and asks the server for nothing', async () => {
  const { unit, server } = await connected({ address: null });
  await turns();
  assert.ok(!unit.commands.includes('attest'));
  assert.deepEqual(server.requests, []);
});

test('firmware from before attest answers unknown_command, which the pilot never sees', async () => {
  const { unit, server, updater } = await connected({ attests: false });
  await turns();
  assert.equal(unit.commands.at(-1), 'attest');
  assert.deepEqual(server.requests, ['attestation_challenge']);
  assert.equal(updater.state.notice, null);
});

test('a server out of reach costs the unit no command, and the page no notice', async () => {
  const server = knowing(new FakeServer());
  server.refuseNext = 'unreachable';
  const { unit, updater } = await connected({ server });
  await turns();
  assert.ok(!unit.commands.includes('attest'));
  assert.equal(updater.state.notice, null);
});

test('the unit refuses what it cannot sign, and the page carries on', async () => {
  const flying = await connected({ flight: 'airborne' });
  await turns();
  assert.deepEqual(flying.server.firmwares, []);
  assert.equal(flying.updater.state.notice, null);

  const narrow = await connected({ mtu: ATT_HEADER_BYTES + ATTESTATION_FRAME_BYTES - 1 });
  await turns();
  assert.deepEqual(narrow.server.firmwares, []);
  assert.equal(narrow.unit.commands.at(-1), 'attest');
});

// A refusal of attest arriving in the middle of an install would read as the install's.
test('a pilot who starts something before the server answers keeps the unit to themselves', async () => {
  const server = knowing(new FakeServer());
  server.hold();
  const { unit, updater, until } = await connected({ server });
  await updater.choose(signedImage(NEWER, 6000), 'skyblip_go-0.2.0+15.signed.bin');
  updater.install();
  await until(asking('dfu'));
  server.release();
  await turns();
  assert.ok(!unit.commands.includes('attest'));
  assert.equal(updater.state.task, 'dfu');
});

test('a statement the active key did not sign is refused by the server and recorded nowhere', async () => {
  const server = new FakeServer();
  server.units.set(VECTORS.device_id, { serial: ADDRESS, publicKey: keyPair('11'.repeat(32)).publicKey });
  const { unit } = await connected({ server });
  await turns();
  assert.equal(unit.commands.at(-1), 'attest');
  assert.deepEqual(server.requests, ['attestation_challenge', 'attestation']);
  assert.deepEqual(server.firmwares, []);
});
