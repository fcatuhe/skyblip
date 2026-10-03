import test from 'node:test';
import assert from 'node:assert/strict';

import { BLOB_PIN, signedImage } from './fake_skyblip.mjs';
import { fullImageNeeded, readImage } from './image.js';

const VERSION = { major: 0, minor: 2, revision: 0, build: 15 };
const IMAGE_BYTES = 6000;
const PREFIX = BLOB_PIN.slice(0, 16);

test('the protected TLVs give the blob an image pins and whether it carries it', () => {
  assert.deepEqual(readImage(signedImage(VERSION, IMAGE_BYTES, { imu: { pin: BLOB_PIN, full: false } })).imu, { pin: BLOB_PIN, full: false });
  assert.deepEqual(readImage(signedImage(VERSION, IMAGE_BYTES, { imu: { pin: BLOB_PIN, full: true } })).imu, { pin: BLOB_PIN, full: true });
  assert.deepEqual(readImage(signedImage(VERSION, IMAGE_BYTES, { imu: { full: true } })).imu, { pin: null, full: true });
});

test('an image from before the split has no protected area, so no pin is known', () => {
  const image = readImage(signedImage(VERSION, IMAGE_BYTES));
  assert.deepEqual(image.version, VERSION);
  assert.deepEqual(image.imu, { pin: null, full: null });
});

test('a protected area cut short or with an entry overrunning it reads as no pin, never throws', () => {
  const bytes = signedImage(VERSION, IMAGE_BYTES, { imu: { pin: BLOB_PIN, full: false } });
  assert.deepEqual(readImage(bytes.subarray(0, bytes.length - 50)).imu, { pin: null, full: null });
  const overrun = Uint8Array.from(bytes);
  // The pin's length field ends 40 (trailer with its key hash) + 5 (full entry) + 32 (pin) bytes from the end.
  new DataView(overrun.buffer).setUint16(overrun.length - (40 + 5 + 32 + 2), 0x4000, true);
  assert.deepEqual(readImage(overrun).imu, { pin: null, full: null });
  assert.deepEqual(readImage(Uint8Array.from(bytes).fill(0xff, 8, 16)).imu, { pin: null, full: null });
});

for (const [deviceImu, pin, needed] of [
  ['none', null, false],
  ['none', BLOB_PIN, false],
  [PREFIX, BLOB_PIN, false],
  [PREFIX, 'a'.repeat(64), true],
  [PREFIX, null, true],
  ['missing', BLOB_PIN, true],
  ['corrupt', BLOB_PIN, true],
  ['unreadable', BLOB_PIN, true],
  ['writing', BLOB_PIN, true],
  [null, BLOB_PIN, true],
]) {
  test(`a device reporting ${deviceImu} ${needed ? 'needs' : 'does without'} the full image pinning ${pin && pin.slice(0, 4)}`, () => {
    assert.equal(fullImageNeeded(deviceImu, pin), needed);
  });
}
