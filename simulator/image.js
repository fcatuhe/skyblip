const MCUBOOT_MAGIC = 0x96f3b83d;
const HEADER_BYTES = 32;
const VERSION_OFFSET = 20;

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
    bytes: bytes.length,
  };
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
