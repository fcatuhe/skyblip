const MCUBOOT_MAGIC = 0x96f3b83d;
const HEADER_BYTES = 32;
const HEADER_SIZE_OFFSET = 8;
const IMAGE_SIZE_OFFSET = 12;
const VERSION_OFFSET = 20;
const PROTECTED_TLV_MAGIC = 0x6908;
const TLV_INFO_BYTES = 4;
const TLV_IMU_PIN = 0x00a0;
const TLV_IMU_FULL = 0x00a1;
const PIN_BYTES = 32;
const DIGEST_PREFIX = /^[0-9a-f]{16}$/;

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
    imu: imuOf(view),
  };
}

function imuOf(view) {
  const imu = { pin: null, full: null };
  const start = view.getUint16(HEADER_SIZE_OFFSET, true) + view.getUint32(IMAGE_SIZE_OFFSET, true);
  if (start + TLV_INFO_BYTES > view.byteLength || view.getUint16(start, true) !== PROTECTED_TLV_MAGIC) return imu;
  const end = start + view.getUint16(start + 2, true);
  if (end > view.byteLength) return imu;
  for (let at = start + TLV_INFO_BYTES; at + TLV_INFO_BYTES <= end;) {
    const type = view.getUint16(at, true);
    const length = view.getUint16(at + 2, true);
    const value = at + TLV_INFO_BYTES;
    if (value + length > end) return { pin: null, full: null };
    if (type === TLV_IMU_PIN && length === PIN_BYTES) imu.pin = hex(new Uint8Array(view.buffer, view.byteOffset + value, length));
    if (type === TLV_IMU_FULL && length === 1) imu.full = view.getUint8(value) === 1;
    at = value + length;
  }
  return imu;
}

function hex(bytes) {
  return Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');
}

export function fullImageNeeded(deviceImu, pin) {
  if (deviceImu === 'none') return false;
  const verified = DIGEST_PREFIX.test(deviceImu ?? '');
  return !(verified && pin?.startsWith(deviceImu));
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
