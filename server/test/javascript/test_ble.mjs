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

// INFO: fc 14nov26 a tap queues its subscriptions on the link once its link resolves, so a write sent a turn later lands behind them
async function behindTheTap(client) {
  await new Promise(resolve => setImmediate(resolve));
  await client.sendConfig({ command: 'status' });
}

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
  const untap = tap(device, entry => heard.push(entry));
  await behindTheTap(updater);
  fake.log.notify(encodeFrame({ command: 'log', sessions: 0 }));
  assert.deepEqual(heard.map(({ endpoint, direction }) => `${endpoint} ${direction}`), ['config out', 'log in']);
  untap();
  fake.drop();
});

// An open terminal must never cost an install: a tap that throws, or one placed mid-upload, leaves the writes alone.
test('a tap that throws fails no write and no connect, and says so on the console', async () => {
  const fake = new FakeSkyblip();
  const device = fake.device();
  const untap = tap(device, () => {
    throw new Error('the terminal broke');
  });
  const logged = [];
  const { error } = console;
  console.error = (...args) => logged.push(args);
  try {
    const updater = await connect({ device, onReply: () => {}, onSmp: () => {}, onClose: () => {} });
    await updater.sendConfig({ command: 'status' });
    await updater.sendSmp(packet({ op: OP.read, group: GROUP.os, id: ID.osParams, seq: 0, body: {} }));
  } finally {
    console.error = error;
  }
  assert.equal(fake.commands.at(-1), 'status');
  assert.ok(logged.length >= 3);
  assert.equal(logged[0][1].message, 'the terminal broke');
  untap();
  fake.drop();
});

test('a tap placed mid-upload subscribes between writes, never across one', async () => {
  const fake = new FakeSkyblip();
  const device = fake.device();
  const updater = await connect({ device, onReply: () => {}, onSmp: () => {}, onClose: () => {} });
  const upload = updater.sendSmp(packet({ op: OP.read, group: GROUP.image, id: ID.imageState, seq: 0, body: { pad: 'x'.repeat(200) } }));
  const untap = tap(device, () => {});
  await upload;
  await behindTheTap(updater);
  assert.equal(fake.log.notifying, true);
  assert.equal(fake.overlaps, 0);
  untap();
  fake.drop();
});

test('a write a client makes in answer to a notification prints after it', async () => {
  const fake = new FakeSkyblip();
  const device = fake.device();
  const heard = [];
  const untap = tap(device, ({ endpoint, direction, frame }) => endpoint && heard.push([endpoint, direction, frame?.command].filter(Boolean).join(' ')));
  let updater;
  const answer = reply => reply.command === 'status' && updater.sendSmp(packet({ op: OP.read, group: GROUP.os, id: ID.osParams, seq: 0, body: {} }));
  updater = await connect({ device, onReply: answer, onSmp: () => {}, onClose: () => {} });
  await updater.sendConfig({ command: 'status' });
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(heard.slice(0, 3), ['config out', 'config in status', 'smp out']);
  untap();
  fake.drop();
});

test('a tap placed while the link opens hears it open once, and an endpoint that will not subscribe fails once', async () => {
  const fake = new FakeSkyblip();
  const device = fake.device();
  fake.log.startNotifications = async () => {
    throw new Error('GATT Error: Not supported.');
  };
  const heard = [];
  const connecting = connect({ device, onReply: () => {}, onClose: () => {} });
  const untap = tap(device, ({ event, endpoint }) => event && heard.push([event, endpoint].filter(Boolean).join(' ')));
  await behindTheTap(await connecting);
  assert.deepEqual(heard, ['open', 'failed log']);
  untap();
  fake.drop();
});

test('a write the browser refuses prints as failed, and the client still sees the refusal', async () => {
  const fake = new FakeSkyblip({ mtu: 23, writeCeiling: 20, oversize: 'reject' });
  const device = fake.device();
  const heard = [];
  const untap = tap(device, entry => heard.push(entry));
  const updater = await connect({ device, onReply: () => {}, onSmp: () => {}, onClose: () => {} });
  await assert.rejects(updater.sendSmp(packet({ op: OP.read, group: GROUP.os, id: ID.echo, seq: 0, body: { d: 'x'.repeat(40) } }), 64), /GATT operation failed/);
  const failed = heard.find(entry => entry.event === 'failed');
  assert.equal(failed.endpoint, 'smp');
  assert.match(failed.error.message, /GATT operation failed/);
  untap();
  fake.drop();
});

// NMEA streams several lines a second, so the endpoints only a terminal wanted must go quiet when it leaves.
test('the last tap to let go stops the endpoints only taps wanted, and a client that wants one keeps it', async () => {
  const fake = new FakeSkyblip();
  const device = fake.device();
  const updater = await connect({ device, onReply: () => {}, onClose: () => {} });
  const first = tap(device, () => {});
  const second = tap(device, () => {});
  await behindTheTap(updater);
  first();
  await behindTheTap(updater);
  assert.equal(fake.log.notifying, true);
  second();
  await behindTheTap(updater);
  assert.equal(fake.log.notifying, false);
  assert.equal(fake.log.listeners.length, 0);

  const reader = await connect({ device, onLog: () => {}, onClose: () => {} });
  const third = tap(device, () => {});
  await behindTheTap(updater);
  third();
  await behindTheTap(updater);
  assert.equal(fake.log.notifying, true);
  reader.release();
  fake.drop();
});
