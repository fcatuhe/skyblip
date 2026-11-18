import { imageEnd, readImage } from '#skyblip/image';

const BLOCK_BYTES = 512;
const DATA_OFFSET = 32;
const DATA_ROOM = 476;
const PAYLOAD_BYTES = 256;
const MAGIC_START0 = 0x0a324655;
const MAGIC_START1 = 0x9e5d5157;
const MAGIC_END = 0x0ab16f30;
const FLAG_FAMILY = 0x00002000;
const FLAG_NOT_MAIN_FLASH = 0x00000001;
export const PAGE_BYTES = 4096;

// INFO: fc 17nov26 slot0 of firmware/boards t_echo_plus.dts and meshtracker_x1.dts, whose last sector holds MCUboot's trailer
export const LAYOUTS = {
  go: { slot0: 0x34000, slotBytes: 0xb6000 },
  bravo: { slot0: 0x35000, slotBytes: 0xb5000 },
};

// INFO: fc 17nov26 the Board-ID lines of each factory INFO_UF2.TXT, firmware/boards/*/factory and meshtracker_x1/README.md
const BOARDS = [
  [/^nRF52840-TEcho/i, 'go'],
  [/^nRF52840-MeshTracker-X1/i, 'bravo'],
];

export const RECORD_MAGIC = 'skyblip:trust:v1';
const KEY_BYTES = 32;
const LABEL_BYTES = 24;
export const RECORD_BYTES = RECORD_MAGIC.length + KEY_BYTES + 1 + LABEL_BYTES + 4;

export function boardOf(info) {
  const id = /^Board-ID:\s*(\S+)/m.exec(info ?? '')?.[1] ?? null;
  return { id, product: BOARDS.find(([pattern]) => pattern.test(id ?? ''))?.[1] ?? null };
}

export function parseUf2(bytes) {
  if (bytes.length === 0 || bytes.length % BLOCK_BYTES !== 0) throw new Uf2Error('not_uf2');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const blocks = [];
  let family = null;
  for (let at = 0; at < bytes.length; at += BLOCK_BYTES) {
    if (view.getUint32(at, true) !== MAGIC_START0 || view.getUint32(at + 4, true) !== MAGIC_START1 || view.getUint32(at + 508, true) !== MAGIC_END) throw new Uf2Error('not_uf2');
    const flags = view.getUint32(at + 8, true);
    if (flags & FLAG_NOT_MAIN_FLASH) continue;
    const size = view.getUint32(at + 16, true);
    if (size > DATA_ROOM) throw new Uf2Error('not_uf2');
    if (flags & FLAG_FAMILY) family = view.getUint32(at + 28, true);
    blocks.push({ address: view.getUint32(at + 12, true), data: bytes.slice(at + DATA_OFFSET, at + DATA_OFFSET + size) });
  }
  return { family, blocks };
}

export function readSpan(blocks, address, length) {
  const span = new Uint8Array(length).fill(0xff);
  for (const block of blocks) {
    const from = Math.max(block.address, address);
    const to = Math.min(block.address + block.data.length, address + length);
    if (from < to) span.set(block.data.subarray(from - block.address, to - block.address), from - address);
  }
  return span;
}

export function encodeUf2({ family, blocks }) {
  const out = new Uint8Array(blocks.length * BLOCK_BYTES);
  const view = new DataView(out.buffer);
  blocks.forEach((block, index) => {
    const at = index * BLOCK_BYTES;
    view.setUint32(at, MAGIC_START0, true);
    view.setUint32(at + 4, MAGIC_START1, true);
    view.setUint32(at + 8, family === null ? 0 : FLAG_FAMILY, true);
    view.setUint32(at + 12, block.address, true);
    view.setUint32(at + 16, block.data.length, true);
    view.setUint32(at + 20, index, true);
    view.setUint32(at + 24, blocks.length, true);
    view.setUint32(at + 28, family ?? 0, true);
    out.set(block.data, at + DATA_OFFSET);
    view.setUint32(at + 508, MAGIC_END, true);
  });
  return out;
}

const CRC_TABLE = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});

export function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) crc = CRC_TABLE[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function clippedLabel(label) {
  const encoder = new TextEncoder();
  let clipped = label ?? '';
  while (encoder.encode(clipped).length > LABEL_BYTES) clipped = [...clipped].slice(0, -1).join('');
  return encoder.encode(clipped);
}

export function trustRecord({ publicKey, label }) {
  if (publicKey?.length !== KEY_BYTES) throw new Uf2Error('no_key');
  const record = new Uint8Array(RECORD_BYTES);
  const labelBytes = clippedLabel(label);
  let at = 0;
  record.set(new TextEncoder().encode(RECORD_MAGIC), at);
  at += RECORD_MAGIC.length;
  record.set(publicKey, at);
  at += KEY_BYTES;
  record[at++] = labelBytes.length;
  record.set(labelBytes, at);
  at += LABEL_BYTES;
  new DataView(record.buffer).setUint32(at, crc32(record.subarray(0, at)), true);
  return record;
}

export function recordPage(blocks, { slot0, slotBytes }) {
  const slot = readSpan(blocks, slot0, slotBytes);
  const end = imageEnd(slot);
  if (end === null) throw new Uf2Error('no_image');
  const page = slot0 + Math.ceil(end / PAGE_BYTES) * PAGE_BYTES;
  if (page + PAGE_BYTES > slot0 + slotBytes - PAGE_BYTES) throw new Uf2Error('no_room');
  return page;
}

export function imageOf(blocks, { slot0, slotBytes }) {
  return readImage(readSpan(blocks, slot0, slotBytes));
}

// INFO: fc 17nov26 the record's page replaces whatever the release wrote there, so the bootloader sees one block per address
export function withTrustRecord(uf2, layout, record) {
  const page = recordPage(uf2.blocks, layout);
  const content = new Uint8Array(PAGE_BYTES).fill(0xff);
  content.set(record);
  const kept = uf2.blocks.filter(block => block.address + block.data.length <= page || block.address >= page + PAGE_BYTES);
  const outside = kept.length === uf2.blocks.length ? [] : uf2.blocks.filter(block => !kept.includes(block)).flatMap(block => clip(block, page));
  const written = Array.from({ length: PAGE_BYTES / PAYLOAD_BYTES }, (_, index) => ({
    address: page + index * PAYLOAD_BYTES,
    data: content.slice(index * PAYLOAD_BYTES, (index + 1) * PAYLOAD_BYTES),
  }));
  const blocks = [...kept, ...outside, ...written].sort((a, b) => a.address - b.address);
  return { family: uf2.family, blocks, page };
}

function clip(block, page) {
  const parts = [];
  const end = block.address + block.data.length;
  if (block.address < page) parts.push({ address: block.address, data: block.data.slice(0, page - block.address) });
  if (end > page + PAGE_BYTES) parts.push({ address: page + PAGE_BYTES, data: block.data.slice(page + PAGE_BYTES - block.address) });
  return parts;
}

export class Uf2Error extends Error {
  constructor(key) {
    super(key);
    this.key = key;
  }
}
