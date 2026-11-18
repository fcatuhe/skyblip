import { LAYOUTS, PAGE_BYTES, encodeUf2 } from '#skyblip/uf2';
import { signedImage } from './fake_image.mjs';

export const UF2_FAMILY_NRF52840 = 0xada52840;
const MCUBOOT_START = { go: 0x26000, bravo: 0x27000 };
const PAYLOAD_BYTES = 256;
const MCUBOOT_MAGIC = Uint8Array.from('77c295f360d2ef7f3552500f2cb67980'.match(/../g), pair => parseInt(pair, 16));

function blocksOf(address, bytes) {
  const blocks = [];
  for (let at = 0; at < bytes.length; at += PAYLOAD_BYTES) blocks.push({ address: address + at, data: bytes.slice(at, at + PAYLOAD_BYTES) });
  return blocks;
}

// INFO: fc 17nov26 what mkuf2.py merges: MCUboot, the confirmed image at slot0, and slot0's last sector written whole
export function releaseUf2(product, { imageBytes = 200_000, version = { major: 0, minor: 1, revision: 0, build: 1858 }, imageProduct = product } = {}) {
  const { slot0, slotBytes } = LAYOUTS[product];
  const mcuboot = new Uint8Array(0x8000).map((_, at) => (at * 13) & 0xff);
  const image = signedImage(version, imageBytes, { product: imageProduct });
  const trailer = new Uint8Array(PAGE_BYTES).fill(0xff);
  trailer.set(MCUBOOT_MAGIC, PAGE_BYTES - MCUBOOT_MAGIC.length);
  trailer[PAGE_BYTES - 24] = 0x01;
  const blocks = [...blocksOf(MCUBOOT_START[product], mcuboot), ...blocksOf(slot0, image), ...blocksOf(slot0 + slotBytes - PAGE_BYTES, trailer)];
  return { bytes: encodeUf2({ family: UF2_FAMILY_NRF52840, blocks }), image };
}
