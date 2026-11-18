import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { imageEnd } from '#skyblip/image';
import { LAYOUTS, PAGE_BYTES, RECORD_BYTES, boardOf, crc32, encodeUf2, imageOf, parseUf2, readSpan, recordPage, trustRecord, withTrustRecord } from '#skyblip/uf2';
import { UF2_FAMILY_NRF52840, releaseUf2 } from './fake_uf2.mjs';

const KEY = Uint8Array.from({ length: 32 }, (_, at) => at + 1);
const VECTORS = JSON.parse(readFileSync(new URL('../../../schemas/trust_record.v1.vectors.json', import.meta.url), 'utf8'));
const fromHex = text => Uint8Array.from(text.match(/../g), pair => parseInt(pair, 16));
const toHex = bytes => [...bytes].map(byte => byte.toString(16).padStart(2, '0')).join('');

test('a UF2 reads back to the blocks it was written from, family included', () => {
  const { bytes } = releaseUf2('go');
  const uf2 = parseUf2(bytes);
  assert.equal(uf2.family, UF2_FAMILY_NRF52840);
  assert.deepEqual(encodeUf2(uf2), bytes);
});

test('a file that is not UF2 is refused rather than written to a drive', () => {
  assert.throws(() => parseUf2(new Uint8Array(512)), { key: 'not_uf2' });
  assert.throws(() => parseUf2(new Uint8Array(100)), { key: 'not_uf2' });
});

// The check value of CRC-32/ISO-HDLC, the CRC zlib computes.
test('the record is sealed by the CRC zlib computes', () => {
  assert.equal(crc32(new TextEncoder().encode('123456789')), 0xcbf43926);
});

test('the record is the one the firmware reads, byte for byte, from the shared vectors', () => {
  assert.equal(toHex(trustRecord({ publicKey: fromHex(VECTORS.key), label: VECTORS.label })), VECTORS.record);
  assert.notEqual(toHex(trustRecord({ publicKey: fromHex(VECTORS.key), label: VECTORS.label })), VECTORS.record_with_a_bad_crc);
});

test('the record page lands where the firmware looks for it, on both boards, and not past the X1 budget', () => {
  const at = { header: VECTORS.header_bytes, image: VECTORS.image_bytes, protectedTlvs: VECTORS.protected_tlv_bytes, tlvs: VECTORS.tlv_total };
  const end = at.header + at.image + at.protectedTlvs + at.tlvs;
  assert.equal(end, VECTORS.image_end);
  for (const [product, page] of [['go', VECTORS.t_echo_page_address], ['bravo', VECTORS.x1_page_address]]) {
    const { bytes } = releaseUf2(product, { imageBytes: end });
    assert.equal(recordPage(parseUf2(bytes).blocks, LAYOUTS[product]), page);
  }
  assert.deepEqual(LAYOUTS.bravo, { slot0: VECTORS.x1_slot0_start, slotBytes: VECTORS.x1_slot0_bytes });
  assert.deepEqual(LAYOUTS.go, { slot0: VECTORS.t_echo_slot0_start, slotBytes: VECTORS.t_echo_slot0_bytes });
  assert.doesNotThrow(() => recordPage(parseUf2(releaseUf2('bravo', { imageBytes: VECTORS.x1_last_image_end }).bytes).blocks, LAYOUTS.bravo));
  assert.throws(() => recordPage(parseUf2(releaseUf2('bravo', { imageBytes: VECTORS.x1_first_image_end_with_no_page }).bytes).blocks, LAYOUTS.bravo), { key: 'no_room' });
});

test('the record carries the magic, the key, the label and a CRC over them, in 77 bytes', () => {
  const record = trustRecord({ publicKey: KEY, label: 'Chrome on Linux' });
  assert.equal(record.length, RECORD_BYTES);
  assert.equal(RECORD_BYTES, 77);
  assert.equal(new TextDecoder().decode(record.subarray(0, 16)), 'skyblip:trust:v1');
  assert.deepEqual(record.subarray(16, 48), KEY);
  assert.equal(record[48], 15);
  assert.equal(new TextDecoder().decode(record.subarray(49, 64)), 'Chrome on Linux');
  assert.ok(record.subarray(64, 73).every(byte => byte === 0));
  assert.equal(new DataView(record.buffer).getUint32(73, true), crc32(record.subarray(0, 73)));
});

test('a label longer than 24 bytes is cut on a character, as the unit stores at most 24', () => {
  const record = trustRecord({ publicKey: KEY, label: 'Microsoft Edge on Windows 11' });
  assert.equal(record[48], 24);
  assert.equal(new TextDecoder().decode(record.subarray(49, 73)), 'Microsoft Edge on Window');
});

test('the record page is the first page after the image and its TLVs', () => {
  const { bytes, image } = releaseUf2('go', { imageBytes: 200_000 });
  const { slot0 } = LAYOUTS.go;
  assert.equal(imageEnd(image), 200_000);
  assert.equal(recordPage(parseUf2(bytes).blocks, LAYOUTS.go), slot0 + Math.ceil(200_000 / PAGE_BYTES) * PAGE_BYTES);
});

test('an image that ends on a page boundary puts the record on the page right after it', () => {
  const { bytes } = releaseUf2('bravo', { imageBytes: 49 * PAGE_BYTES });
  assert.equal(recordPage(parseUf2(bytes).blocks, LAYOUTS.bravo), LAYOUTS.bravo.slot0 + 49 * PAGE_BYTES);
});

test('the rewritten UF2 keeps every other byte and writes the record page whole', () => {
  const { bytes, image } = releaseUf2('go');
  const record = trustRecord({ publicKey: KEY, label: 'Chrome on Linux' });
  const rewritten = withTrustRecord(parseUf2(bytes), LAYOUTS.go, record);
  const back = parseUf2(encodeUf2(rewritten));
  const { slot0, slotBytes } = LAYOUTS.go;
  assert.deepEqual(readSpan(back.blocks, slot0, image.length), image);
  const page = readSpan(back.blocks, rewritten.page, PAGE_BYTES);
  assert.deepEqual(page.subarray(0, RECORD_BYTES), record);
  assert.ok(page.subarray(RECORD_BYTES).every(byte => byte === 0xff));
  assert.deepEqual(readSpan(back.blocks, slot0 + slotBytes - PAGE_BYTES, PAGE_BYTES), readSpan(parseUf2(bytes).blocks, slot0 + slotBytes - PAGE_BYTES, PAGE_BYTES));
  const addresses = back.blocks.map(block => block.address);
  assert.equal(new Set(addresses).size, addresses.length);
});

test('a release that already writes the record page gets it replaced, one block per address', () => {
  const { bytes } = releaseUf2('go');
  const uf2 = parseUf2(bytes);
  const page = recordPage(uf2.blocks, LAYOUTS.go);
  uf2.blocks.push({ address: page - 128, data: new Uint8Array(256).fill(0xab) });
  const rewritten = withTrustRecord(uf2, LAYOUTS.go, trustRecord({ publicKey: KEY, label: '' }));
  assert.ok(readSpan(rewritten.blocks, page - 128, 128).every(byte => byte === 0xab));
  assert.equal(readSpan(rewritten.blocks, page, 1)[0], 's'.charCodeAt(0));
});

test('an image that leaves no page before the trailer sector carries no record', () => {
  const { slotBytes } = LAYOUTS.go;
  const { bytes } = releaseUf2('go', { imageBytes: slotBytes - PAGE_BYTES - 100 });
  assert.throws(() => recordPage(parseUf2(bytes).blocks, LAYOUTS.go), { key: 'no_room' });
});

test('the image inside a UF2 names its product', () => {
  assert.equal(imageOf(parseUf2(releaseUf2('bravo').bytes).blocks, LAYOUTS.bravo).product, 'bravo');
});

test('the drive says which board it is in INFO_UF2.TXT', () => {
  const factory = path => readFileSync(new URL(`../../../firmware/boards/${path}/INFO_UF2.TXT`, import.meta.url), 'utf8');
  assert.deepEqual(boardOf(factory('lilygo/t_echo_plus/factory')), { id: 'nRF52840-TEcho-v1', product: 'go' });
  assert.deepEqual(boardOf(factory('lilygo/t_echo_plus/factory/t_echo')), { id: 'nRF52840-TEcho-v1', product: 'go' });
  assert.deepEqual(boardOf(factory('seeed/meshtracker_x1/factory')), { id: 'nRF52840-MeshTracker-X1-v1', product: 'bravo' });
  assert.deepEqual(boardOf('Board-ID: nRF52840-MeshTracker-X1\n'), { id: 'nRF52840-MeshTracker-X1', product: 'bravo' });
  assert.deepEqual(boardOf('Board-ID: nRF52840-Feather\n'), { id: 'nRF52840-Feather', product: null });
  assert.deepEqual(boardOf(null), { id: null, product: null });
});
