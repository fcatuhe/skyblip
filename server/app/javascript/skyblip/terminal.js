import { decode } from '#skyblip/cbor';
import { decodeFrame, hex, printable } from '#skyblip/link';
import { HEADER_BYTES, parseHeader } from '#skyblip/smp';

export const ENDPOINTS = { stream: 'nmea', config: 'config', log: 'log', smp: 'smp' };
// INFO: fc 14nov26 an image chunk is hundreds of bytes, so a line shows its first bytes and its length
const SHOWN_BYTES = 16;
const SMP_OPS = ['read', 'read reply', 'write', 'write reply'];
const SMP_COMMANDS = { 0: { 0: 'os echo', 6: 'os params' }, 1: { 0: 'image state', 1: 'image upload' } };
const decoder = new TextDecoder();

export function lineOf(entry) {
  if (entry.event) return { event: entry.event, text: entry.error?.message ?? '' };
  return { endpoint: ENDPOINTS[entry.endpoint], direction: entry.direction, bad: false, ...TEXTS[entry.endpoint](entry) };
}

const TEXTS = {
  stream: ({ line, bytes }) => ({ text: line ?? decoder.decode(bytes).trimEnd() }),
  config: frameText,
  log: frameText,
  smp: ({ bytes }) => smpText(bytes),
};

function frameText({ frame, bytes, error }) {
  if (frame) return { text: printable(frame) };
  if (error) return { text: `${error.message}: ${hex(bytes)}`, bad: true };
  try {
    return { text: printable(decodeFrame(bytes)) };
  } catch (refusal) {
    return { text: `${refusal.message}: ${hex(bytes)}`, bad: true };
  }
}

// INFO: fc 14nov26 smp_bt cuts a reply at MTU-3, so a notification that is not one whole packet is a slice of one
function smpText(bytes) {
  const header = bytes.length >= HEADER_BYTES ? parseHeader(bytes) : null;
  if (!header || HEADER_BYTES + header.length !== bytes.length) return { text: `slice ${shown(bytes)}` };
  const command = SMP_COMMANDS[header.group]?.[header.id] ?? `group ${header.group} id ${header.id}`;
  const op = SMP_OPS[header.op] ?? `op ${header.op}`;
  const payload = bytes.subarray(HEADER_BYTES);
  try {
    return { text: `${op} ${command} seq ${header.seq} ${JSON.stringify(decode(payload), bytesShown)}` };
  } catch (error) {
    return { text: `${op} ${command} seq ${header.seq} ${error.message}: ${shown(payload)}`, bad: true };
  }
}

function bytesShown(key, value) {
  return value instanceof Uint8Array ? shown(value) : value;
}

function shown(bytes) {
  if (bytes.length <= SHOWN_BYTES) return hex(bytes);
  return `${hex(bytes.subarray(0, SHOWN_BYTES))}... (${bytes.length} bytes)`;
}
