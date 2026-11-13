import test from 'node:test';
import assert from 'node:assert/strict';

import { FrameError, encodeFrame } from '#skyblip/link';
import { GROUP, ID, OP, packet } from '#skyblip/smp';
import { lineOf } from '#skyblip/terminal';

test('a config frame reads by name both ways, the one written decoded from its bytes', () => {
  const sent = lineOf({ endpoint: 'config', direction: 'out', bytes: encodeFrame({ command: 'get' }) });
  assert.deepEqual(sent, { endpoint: 'config', direction: 'out', bad: false, text: '{"command":"get"}' });
  const heard = lineOf({ endpoint: 'log', direction: 'in', frame: { command: 'log', log: 'diagnostics', sessions: 2 }, bytes: null, error: null });
  assert.equal(heard.endpoint, 'log');
  assert.equal(heard.text, '{"command":"log","log":"diagnostics","sessions":2}');
});

test('a notification outside the registry shows its bytes and why, marked bad', () => {
  const line = lineOf({ endpoint: 'config', direction: 'in', frame: null, bytes: Uint8Array.of(0xa1, 0x00, 0x18, 0x05), error: new FrameError('not shortest form') });
  assert.equal(line.text, 'not shortest form: a1001805');
  assert.equal(line.bad, true);
});

test('the serial endpoint is NMEA, a line heard as it came and one written without its line end', () => {
  assert.deepEqual(lineOf({ endpoint: 'stream', direction: 'in', line: '$PFLAU,0*4F' }), { endpoint: 'nmea', direction: 'in', bad: false, text: '$PFLAU,0*4F' });
  assert.equal(lineOf({ endpoint: 'stream', direction: 'out', bytes: new TextEncoder().encode('$PSRFC,?*47\r\n') }).text, '$PSRFC,?*47');
});

// An image chunk is hundreds of bytes, and a line that printed them all would hide every other frame.
test('an SMP packet names its command, and a long byte string shows its start and length', () => {
  const data = Uint8Array.from({ length: 470 }, (_, at) => at % 256);
  const bytes = packet({ op: OP.write, group: GROUP.image, id: ID.imageUpload, seq: 7, body: { off: 940, data } });
  const line = lineOf({ endpoint: 'smp', direction: 'out', bytes });
  assert.equal(line.text, 'write image upload seq 7 {"off":940,"data":"000102030405060708090a0b0c0d0e0f... (470 bytes)"}');
});

test('an SMP notification that is not one whole packet is a slice', () => {
  const whole = packet({ op: OP.readResponse, group: GROUP.os, id: ID.echo, seq: 1, body: { r: 'x'.repeat(40) } });
  const line = lineOf({ endpoint: 'smp', direction: 'in', bytes: whole.subarray(0, 20) });
  assert.equal(line.text, `slice ${Buffer.from(whole.subarray(0, 16)).toString('hex')}... (20 bytes)`);
  assert.equal(lineOf({ endpoint: 'smp', direction: 'in', bytes: whole }).text, `read reply os echo seq 1 {"r":"${'x'.repeat(40)}"}`);
});

test('a link event that failed names its endpoint and why, marked bad', () => {
  assert.deepEqual(lineOf({ event: 'close' }), { event: 'close', bad: false, text: '' });
  assert.deepEqual(lineOf({ event: 'failed', endpoint: 'stream', error: new Error('GATT Error') }), { event: 'failed', bad: true, text: 'nmea: GATT Error' });
});

test('an SMP reply that refuses is marked bad', () => {
  const refusal = packet({ op: OP.writeResponse, group: GROUP.image, id: ID.imageUpload, seq: 3, body: { err: { group: 1, rc: 27 } } });
  const line = lineOf({ endpoint: 'smp', direction: 'in', bytes: refusal });
  assert.equal(line.text, 'write reply image upload seq 3 {"err":{"group":1,"rc":27}}');
  assert.equal(line.bad, true);
  assert.equal(lineOf({ endpoint: 'smp', direction: 'in', bytes: packet({ op: OP.readResponse, group: GROUP.os, id: ID.osParams, seq: 0, body: { buf_size: 2475 } }) }).bad, false);
});
