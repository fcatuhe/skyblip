import test from 'node:test';
import assert from 'node:assert/strict';

import { FrameReader, MeshtasticLink, enterDfuMode, identityOf, readFromRadio, wantConfig } from '#skyblip/meshtastic';
import { FakeNode, configComplete, logLine, metadata, myInfo } from './fake_meshtastic.mjs';

const hex = bytes => [...bytes].map(byte => byte.toString(16).padStart(2, '0')).join(' ');

test('want_config asks for the config only, framed as the serial API frames a ToRadio', () => {
  assert.equal(hex(wantConfig()), '94 c3 00 04 18 ac 9e 04');
});

// The packet goes to node 0x009a3f2a with id 0x01020304, and carries the admin port's enter_dfu_mode_request set true.
test('the restart is an admin packet to our own node asking for DFU mode', () => {
  assert.equal(hex(enterDfuMode(0x9a3f2a, 0x01020304)),
    '94 c3 00 15 0a 13 15 2a 3f 9a 00 22 07 08 06 12 03 a8 01 01 35 04 03 02 01');
});

test('frames are found among log text, and across reads that cut them', () => {
  const frames = [];
  const reader = new FrameReader(payload => frames.push(payload));
  const first = myInfo(0x9a3f2a);
  reader.feed(logLine('INFO | booting \x94 not a frame'));
  reader.feed(first.subarray(0, 2));
  reader.feed(first.subarray(2));
  reader.feed([...logLine('DEBUG | config'), ...configComplete(69420)]);
  assert.equal(frames.length, 2);
  assert.deepEqual(readFromRadio(frames[0]), { myNodeNum: 0x9a3f2a });
  assert.deepEqual(readFromRadio(frames[1]), { configComplete: true });
});

test('a length past the API maximum is noise, and the reader finds the next frame', () => {
  const frames = [];
  const reader = new FrameReader(payload => frames.push(payload));
  reader.feed([0x94, 0xc3, 0xff, 0xff, ...myInfo(7)]);
  assert.deepEqual(frames.map(readFromRadio), [{ myNodeNum: 7 }]);
});

test('the metadata names the board and the firmware', () => {
  const [, , , , ...payload] = metadata(128, '2.7.15.567b8ea');
  assert.deepEqual(readFromRadio(Uint8Array.from(payload)), { hwModel: 128, firmwareVersion: '2.7.15.567b8ea' });
});

test('a T-Echo, a T-Echo Plus and an X1 are the boards skyBlip runs on, tagged by the low two bytes of the node number', () => {
  assert.deepEqual(identityOf({ myNodeNum: 0x9a3f2a, hwModel: 7 }), { nodeNum: 0x9a3f2a, hwModel: 7, product: 'go', tag: 0x3f2a, firmwareVersion: null });
  assert.equal(identityOf({ myNodeNum: 1, hwModel: 33 }).product, 'go');
  assert.equal(identityOf({ myNodeNum: 1, hwModel: 128 }).product, 'bravo');
  assert.equal(identityOf({ myNodeNum: 1, hwModel: 71 }).product, null);
  assert.equal(identityOf({ myNodeNum: 1, nodeModel: 128 }).product, 'bravo');
});

test('a node answers who it is, then restarts into its drive when asked', async () => {
  const node = new FakeNode({ nodeNum: 0xd51b2c, hwModel: 128 });
  const link = new MeshtasticLink(node);
  const identity = await link.identify();
  assert.deepEqual(identity, { nodeNum: 0xd51b2c, hwModel: 128, product: 'bravo', tag: 0x1b2c, firmwareVersion: '2.7.15.567b8ea' });
  await link.restartIntoDrive(identity.nodeNum);
  await link.close();
  assert.equal(node.restarted, true);
});

test('a port that never answers as Meshtastic gives no identity, after the wait', async () => {
  const node = new FakeNode({ answers: false });
  const link = new MeshtasticLink(node);
  assert.equal(await link.identify({ withinMs: 20 }), null);
  await link.close();
  assert.equal(node.restarted, false);
});
