const MCUBOOT_MAGIC = 0x96f3b83d;
const HEADER_BYTES = 32;
const VERSION_OFFSET = 20;
const TLV_MAGIC = 0x6907;
const PROTECTED_TLV_MAGIC = 0x6908;
const TLV_KEYHASH = 0x01;
const KEY_HASH_BYTES = 32;
const KEY_PREFIX_BYTES = 4;

export function readImage(bytes) {
  if (bytes.length < HEADER_BYTES) return null;
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint32(0, true) !== MCUBOOT_MAGIC) return null;
  return {
    version: {
      major: view.getUint8(VERSION_OFFSET),
      minor: view.getUint8(VERSION_OFFSET + 1),
      revision: view.getUint16(VERSION_OFFSET + 2, true),
      build: view.getUint32(VERSION_OFFSET + 4, true),
    },
    key: signingKey(view),
    bytes: bytes.length,
  };
}

// INFO: fc 03oct26 the first four bytes of the KEYHASH TLV, as the device names the key it trusts
function signingKey(view) {
  let at = view.getUint16(8, true) + view.getUint32(12, true);
  if (at + 4 <= view.byteLength && view.getUint16(at, true) === PROTECTED_TLV_MAGIC) at += view.getUint16(at + 2, true);
  if (at + 4 > view.byteLength || view.getUint16(at, true) !== TLV_MAGIC) return null;
  const end = Math.min(at + view.getUint16(at + 2, true), view.byteLength);
  for (at += 4; at + 4 <= end; ) {
    const type = view.getUint16(at, true);
    const length = view.getUint16(at + 2, true);
    at += 4;
    if (type === TLV_KEYHASH && length === KEY_HASH_BYTES && at + length <= end) return hex(view, at, KEY_PREFIX_BYTES);
    at += length;
  }
  return null;
}

function hex(view, at, count) {
  let text = '';
  for (let i = 0; i < count; i++) text += view.getUint8(at + i).toString(16).padStart(2, '0');
  return text.toUpperCase();
}

export function versionText({ major, minor, revision, build }) {
  return `${major}.${minor}.${revision}+${build}`;
}

export function parseVersion(text) {
  const match = /^(\d+)\.(\d+)\.(\d+)(?:[.+](\d+))?$/.exec(text || '');
  if (!match) return null;
  const [major, minor, revision] = match.slice(1, 4).map(Number);
  return { major, minor, revision, build: match[4] === undefined ? 0 : Number(match[4]) };
}

export function compareVersions(a, b) {
  for (const part of ['major', 'minor', 'revision', 'build']) {
    if (a[part] !== b[part]) return a[part] < b[part] ? -1 : 1;
  }
  return 0;
}

export async function sha256(bytes) {
  return new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
}
