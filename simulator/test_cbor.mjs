import test from 'node:test';
import assert from 'node:assert/strict';

import { decode, encode } from './cbor.js';

const hex = text => Uint8Array.from(text.match(/../g) || [], pair => parseInt(pair, 16));
const toHex = bytes => Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');

// RFC 8949 Appendix A, the items both directions agree on.
const VECTORS = [
  [0, '00'],
  [1, '01'],
  [10, '0a'],
  [23, '17'],
  [24, '1818'],
  [25, '1819'],
  [100, '1864'],
  [1000, '1903e8'],
  [1000000, '1a000f4240'],
  [1000000000000, '1b000000e8d4a51000'],
  [-1, '20'],
  [-10, '29'],
  [-100, '3863'],
  [-1000, '3903e7'],
  [false, 'f4'],
  [true, 'f5'],
  [null, 'f6'],
  [new Uint8Array(0), '40'],
  [hex('01020304'), '4401020304'],
  ['', '60'],
  ['a', '6161'],
  ['IETF', '6449455446'],
  ['\u00fc', '62c3bc'],
  ['\u6c34', '63e6b0b4'],
  [[], '80'],
  [[1, 2, 3], '83010203'],
  [[1, [2, 3], [4, 5]], '8301820203820405'],
  [{}, 'a0'],
  [{ a: 1, b: [2, 3] }, 'a26161016162820203'],
];

test('every RFC 8949 vector encodes to its bytes', () => {
  for (const [value, bytes] of VECTORS) assert.equal(toHex(encode(value)), bytes, JSON.stringify(value));
});

test('every RFC 8949 vector decodes to its value', () => {
  for (const [value, bytes] of VECTORS) assert.deepEqual(decode(hex(bytes)), value, bytes);
});

test('indefinite-length items decode, which is how zcbor opens every map', () => {
  assert.deepEqual(decode(hex('5f42010243030405ff')), hex('0102030405'));
  assert.equal(decode(hex('7f657374726561646d696e67ff')), 'streaming');
  assert.deepEqual(decode(hex('9fff')), []);
  assert.deepEqual(decode(hex('9f018202039f0405ffff')), [1, [2, 3], [4, 5]]);
  assert.deepEqual(decode(hex('bf61610161629f0203ffff')), { a: 1, b: [2, 3] });
});

test('floats decode at all three widths', () => {
  assert.equal(decode(hex('f90000')), 0);
  assert.equal(decode(hex('f93c00')), 1);
  assert.equal(decode(hex('f97bff')), 65504);
  assert.equal(decode(hex('f9fc00')), -Infinity);
  assert.equal(decode(hex('fa47c35000')), 100000);
  assert.equal(decode(hex('fb3ff199999999999a')), 1.1);
});

test('a tag is read through to the item it wraps', () => {
  assert.equal(decode(hex('c11a514b67b0')), 1363896240);
});

test('an SMP upload body survives the round trip with its bytes intact', () => {
  const body = { len: 474108, off: 0, sha: new Uint8Array(32).fill(0xab), data: new Uint8Array(300).fill(7) };
  assert.deepEqual(decode(encode(body)), body);
});

test('a truncated item or trailing bytes are refused, not half read', () => {
  assert.throws(() => decode(hex('1903')), RangeError);
  assert.throws(() => decode(hex('a16161')), RangeError);
  assert.throws(() => decode(hex('bf6161')), RangeError);
  assert.throws(() => decode(hex('0001')), RangeError);
});

test('an integer past 2^53 is refused rather than rounded', () => {
  assert.throws(() => decode(hex('1b0020000000000001')), RangeError);
  assert.throws(() => encode(2 ** 53), TypeError);
  assert.throws(() => encode(1.5), TypeError);
});

test('a map key named __proto__ is a key, not a prototype', () => {
  const map = decode(hex('a1695f5f70726f746f5f5f01'));
  assert.equal(Object.getPrototypeOf(map), Object.prototype);
  assert.equal(Object.getOwnPropertyDescriptor(map, '__proto__').value, 1);
});
