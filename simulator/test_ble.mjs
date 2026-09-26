import test from 'node:test';
import assert from 'node:assert/strict';

import { GATT_WRITE_BYTES, chunks, nmeaChecksum, nmeaSentence, reassembleLines } from './ble.js';

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

