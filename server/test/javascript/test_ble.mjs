import test from 'node:test';
import assert from 'node:assert/strict';

import { GATT_WRITE_BYTES, chunks, connect, nmeaSentence, reassembleLines, tap } from '#skyblip/ble';
import { nmeaChecksum } from '#skyblip/nmea';
import { FakeSkyblip } from './fake_skyblip.mjs';
import { GROUP, ID, OP, packet } from '#skyblip/smp';
import { encodeFrame } from '#skyblip/link';

const notification = text => ({ target: { value: new TextEncoder().encode(text) } });

const collect = () => {
  const lines = [];
  return { lines, feed: reassembleLines(line => lines.push(line)) };
};

test('checksum is the XOR between $ and *', () => {
  assert.equal(nmeaChecksum('PSRFC,?'), '47');
  assert.equal(nmeaChecksum('PFLAU,3,1,2,1,1,-45,2,-100,1000,DDA5BA'), '0A');
});

test('a hand-typed sentence leaves with a checksum SoftRF can verify', () => {
  assert.equal(nmeaSentence('$PSRFC,?'), '$PSRFC,?*47');
  assert.equal(nmeaSentence('  $PSRFC,? '), '$PSRFC,?*47');
});

test('a sentence that carries its own checksum is sent untouched', () => {
  assert.equal(nmeaSentence('$PSRFC,?*47'), '$PSRFC,?*47');
});

// A notification is a slice of a stream, so a sentence cut in two must arrive whole.
test('a sentence split across notifications is one line', () => {
  const { lines, feed } = collect();
  feed(notification('$PFLAU,3,1,2,1,1,-4'));
  assert.deepEqual(lines, []);
  feed(notification('5,2,-100,1000,DDA5BA*0A\r\n'));
  assert.deepEqual(lines, ['$PFLAU,3,1,2,1,1,-45,2,-100,1000,DDA5BA*0A']);
});

test('one notification carrying several sentences yields them all', () => {
  const { lines, feed } = collect();
  feed(notification('$PGRMZ,1000,f,3*1A\r\n$LK8EX1,101325,99,0,99,4.05*32\r\n'));
  assert.equal(lines.length, 2);
});

test('a bare newline ends a line and an empty one is dropped', () => {
  const { lines, feed } = collect();
  feed(notification('$PGRMZ,1000,f,3*1A\n\n$PFLAU,0*4F\n'));
  assert.deepEqual(lines, ['$PGRMZ,1000,f,3*1A', '$PFLAU,0*4F']);
});

test('a packet leaves in slices BLE guarantees, in order and whole', () => {
  const packet = Uint8Array.from({ length: 45 }, (_, at) => at);
  const slices = [...chunks(packet)];
  assert.deepEqual(slices.map(slice => slice.length), [GATT_WRITE_BYTES, GATT_WRITE_BYTES, 5]);
  assert.deepEqual(new Uint8Array(slices.flatMap(slice => [...slice])), packet);
});

test('config and SMP writes issued together never overlap on the link', async () => {
  const device = new FakeSkyblip().install();
  const link = await connect({ onReply: () => {}, onSmp: () => {}, onClose: () => {} });
  await Promise.all([
    link.sendConfig({ command: 'status' }),
    link.sendSmp(packet({ op: OP.read, group: GROUP.image, id: ID.imageState, seq: 0, body: { pad: 'x'.repeat(40) } })),
    link.sendConfig({ command: 'update' }),
  ]);
  assert.equal(device.overlaps, 0);
  assert.deepEqual(device.commands, ['status', 'update']);
  link.disconnect();
});

test('a config notification arrives decoded by name, and one that is no frame arrives as its bytes and why', async () => {
  const device = new FakeSkyblip().install();
  const replies = [];
  const link = await connect({ onReply: (...reply) => replies.push(reply), onSmp: () => {}, onClose: () => {} });
  await link.sendConfig({ command: 'status' });
  await new Promise(resolve => setImmediate(resolve));
  device.config.notify(Uint8Array.of(0xa1, 0x00, 0x18, 0x05));
  assert.equal(replies[0][0].command, 'status');
  assert.equal(replies[0][0].flight, 'ground');
  assert.equal(replies[1][0], null);
  assert.deepEqual(replies[1][1], Uint8Array.of(0xa1, 0x00, 0x18, 0x05));
  assert.match(replies[1][2].message, /shortest form/);
  link.disconnect();
});

// A pilot with a Go and a Bravo keeps both connected, and reaches each without the chooser.
test('two devices stay connected side by side', async () => {
  const go = new FakeSkyblip().install();
  const first = await connect({ onReply: () => {}, onClose: () => {} });
  const bravo = new FakeSkyblip({ product: 'skyblip_bravo' }).install();
  const second = await connect({ onReply: () => {}, onClose: () => {} });
  assert.equal(go.connected, true);
  assert.equal(bravo.connected, true);
  first.disconnect();
  assert.equal(go.connected, false);
  assert.equal(bravo.connected, true);
  second.disconnect();
});

test('a device already in hand connects without the chooser', async () => {
  const fake = new FakeSkyblip();
  const device = fake.device();
  globalThis.navigator = { bluetooth: { requestDevice: () => assert.fail('the chooser opened') } };
  const link = await connect({ device, onReply: () => {}, onClose: () => {} });
  assert.equal(fake.connected, true);
  assert.equal(link.device, device);
  link.disconnect();
});

test('two clients of one device share its link, and both hear it close', async () => {
  const fake = new FakeSkyblip();
  const device = fake.device();
  let connects = 0;
  const gattConnect = device.gatt.connect;
  device.gatt.connect = () => {
    connects++;
    return gattConnect();
  };
  const heard = [];
  const closed = [];
  const updater = await connect({ device, onReply: reply => heard.push(['updater', reply.command]), onSmp: () => {}, onClose: () => closed.push('updater') });
  await connect({ device, onReply: reply => heard.push(['activator', reply.command]), onClose: () => closed.push('activator') });
  await updater.sendConfig({ command: 'status' });
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(connects, 1);
  assert.deepEqual(heard, [['updater', 'status'], ['activator', 'status']]);
  fake.drop();
  assert.deepEqual(closed, ['updater', 'activator']);
});

test('a device that dropped connects again on the next request', async () => {
  const fake = new FakeSkyblip();
  const device = fake.device();
  const first = await connect({ device, onReply: () => {}, onClose: () => {} });
  first.disconnect();
  assert.equal(fake.connected, false);
  await connect({ device, onReply: () => {}, onClose: () => {} });
  assert.equal(fake.connected, true);
  fake.drop();
});

// The console and the app share a device: leaving one must not drop the other's link.
test('a client that releases a shared link leaves it up for the others, and the last one closes it', async () => {
  const fake = new FakeSkyblip();
  const device = fake.device();
  const app = await connect({ device, onReply: () => {}, onClose: () => {} });
  const workbench = await connect({ device, onReply: () => {}, onClose: () => {} });
  workbench.release();
  assert.equal(fake.connected, true);
  app.release();
  assert.equal(fake.connected, false);
});

// The terminal listens beside the update client: it hears what that client writes, what the unit answers, and the next link too.
test('a tap hears every client write and every notification, across a reconnect, until it lets go', async () => {
  const fake = new FakeSkyblip();
  const device = fake.device();
  const heard = [];
  const untap = tap(device, ({ event, endpoint, direction, frame }) => heard.push(event ?? [endpoint, direction, frame?.command].filter(Boolean).join(' ')));
  const updater = await connect({ device, onReply: () => {}, onSmp: () => {}, onClose: () => {} });
  await updater.sendConfig({ command: 'status' });
  await updater.sendSmp(packet({ op: OP.read, group: GROUP.os, id: ID.osParams, seq: 0, body: {} }));
  await new Promise(resolve => setImmediate(resolve));
  fake.drop();
  const again = await connect({ device, onReply: () => {}, onClose: () => {} });
  await again.sendConfig({ command: 'update' });
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(heard, ['open', 'config out', 'smp out', 'config in status', 'smp in', 'close', 'open', 'config out', 'config in update']);
  untap();
  await again.sendConfig({ command: 'status' });
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(heard.length, 9);
  fake.drop();
});

test('a tap opens the log endpoint no client asked for, on a link already up', async () => {
  const fake = new FakeSkyblip();
  const device = fake.device();
  const updater = await connect({ device, onReply: () => {}, onClose: () => {} });
  const heard = [];
  tap(device, entry => heard.push(entry));
  await new Promise(resolve => setImmediate(resolve));
  await updater.sendConfig({ command: 'status' });
  fake.log.notify(encodeFrame({ command: 'log', sessions: 0 }));
  assert.deepEqual(heard.map(({ endpoint, direction }) => `${endpoint} ${direction}`), ['config out', 'log in']);
  fake.drop();
});
