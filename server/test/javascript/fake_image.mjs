import { createHash } from 'node:crypto';

export const PRODUCTION_KEY = 0xb2;
const IMAGE_HEADER_BYTES = 32;
const TLV_INFO = 0x6907;
const TLV_KEYHASH = 0x01;
const KEY_HASH_BYTES = 32;
const PROTECTED_TLV_INFO = 0x6908;
const TLV_IMU_PIN = 0x00a0;
const TLV_IMU_FULL = 0x00a1;
const TLV_IMAGE_CLASS = 0x0075;
const DNS_NAMESPACE = '6ba7b8109dad11d180b400c04fd430c8';

export function signedImage({ major, minor, revision, build }, size, { imu = null, key = PRODUCTION_KEY, product = 'skyblip_go' } = {}) {
  const bytes = new Uint8Array(size);
  for (let at = 0; at < size; at++) bytes[at] = (at * 31 + 7) & 0xff;
  const protectedArea = imu || product ? protectedTlvs({ ...imu, product }) : new Uint8Array(0);
  const trailer = tlvArea(TLV_INFO, [[TLV_KEYHASH, new Uint8Array(KEY_HASH_BYTES).fill(key)]]);
  const view = new DataView(bytes.buffer);
  view.setUint32(0, 0x96f3b83d, true);
  view.setUint16(8, IMAGE_HEADER_BYTES, true);
  view.setUint16(10, protectedArea.length, true);
  view.setUint32(12, size - IMAGE_HEADER_BYTES - protectedArea.length - trailer.length, true);
  view.setUint8(20, major);
  view.setUint8(21, minor);
  view.setUint16(22, revision, true);
  view.setUint32(24, build, true);
  bytes.set(protectedArea, size - protectedArea.length - trailer.length);
  bytes.set(trailer, size - trailer.length);
  return bytes;
}

function protectedTlvs({ pin, full, product }) {
  const entries = [];
  if (product) entries.push([TLV_IMAGE_CLASS, imageClass(product)]);
  if (pin !== undefined) entries.push([TLV_IMU_PIN, Uint8Array.from(pin.match(/../g), pair => parseInt(pair, 16))]);
  if (full !== undefined) entries.push([TLV_IMU_FULL, Uint8Array.of(full ? 1 : 0)]);
  return tlvArea(PROTECTED_TLV_INFO, entries);
}

function tlvArea(magic, entries) {
  const area = new Uint8Array(4 + entries.reduce((sum, [, value]) => sum + 4 + value.length, 0));
  const view = new DataView(area.buffer);
  view.setUint16(0, magic, true);
  view.setUint16(2, area.length, true);
  let at = 4;
  for (const [type, value] of entries) {
    view.setUint16(at, type, true);
    view.setUint16(at + 2, value.length, true);
    area.set(value, at + 4);
    at += 4 + value.length;
  }
  return area;
}

export function imageClass(product) {
  return uuid5(uuid5(fromHex(DNS_NAMESPACE), 'skyblip.eu'), product);
}

function uuid5(namespace, name) {
  const digest = createHash('sha1').update(namespace).update(name).digest().subarray(0, 16);
  digest[6] = (digest[6] & 0x0f) | 0x50;
  digest[8] = (digest[8] & 0x3f) | 0x80;
  return Uint8Array.from(digest);
}

function fromHex(text) {
  return Uint8Array.from(text.match(/../g), pair => parseInt(pair, 16));
}
