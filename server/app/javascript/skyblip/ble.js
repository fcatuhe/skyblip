import { decodeFrame, encodeFrame } from '#skyblip/link';
import { nmeaChecksum, nmeaLines } from '#skyblip/nmea';

export const UUID = {
  nus: '6e400001-b5a3-f393-e0a9-e50e24dcca9e',
  nusRx: '6e400002-b5a3-f393-e0a9-e50e24dcca9e',
  nusTx: '6e400003-b5a3-f393-e0a9-e50e24dcca9e',
  hm10: 0xffe0,
  hm10Chr: 0xffe1,
  skyblip: '69c21301-0187-4204-a91d-02eb8858b440',
  config: '69c21302-0187-4204-a91d-02eb8858b440',
  log: '69c21303-0187-4204-a91d-02eb8858b440',
  smp: '8d53dc1d-1db7-4cd3-868b-8a527460aa84',
  smpChr: 'da2e7828-fbce-4e01-ae9e-261174997c48',
};

// INFO: fc 19sep26 Web Bluetooth exposes no MTU, and 20 is what BLE guarantees.
export const GATT_WRITE_BYTES = 20;

const encoder = new TextEncoder();

export function nmeaSentence(text) {
  const line = text.trim();
  if (!line.startsWith('$') || line.includes('*')) return line;
  return line + '*' + nmeaChecksum(line.slice(1));
}

async function serviceOrNull(server, uuid) {
  try {
    return await server.getPrimaryService(uuid);
  } catch {
    return null;
  }
}

export function addressOf(config) {
  return 'address' in config ? config.address.toString(16).toUpperCase().padStart(6, '0') : null;
}

export function hasWebBluetooth() {
  return Boolean(globalThis.navigator && navigator.bluetooth);
}

export function* chunks(bytes, size = GATT_WRITE_BYTES) {
  for (let at = 0; at < bytes.length; at += size) yield bytes.slice(at, at + size);
}

// INFO: fc 26sep26 a write issued while another is in flight can fail as "operation already in progress"
function gattQueue() {
  let tail = Promise.resolve();
  return operation => {
    const run = tail.then(operation);
    tail = run.then(settled, settled);
    return run;
  };
}

function settled() {}

function bytesOf(event) {
  const view = event.target.value;
  return new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
}

export function reassembleLines(onLine) {
  const feed = nmeaLines(onLine);
  return event => feed(bytesOf(event));
}

async function subscribe(chr, listener) {
  await chr.startNotifications();
  chr.addEventListener('characteristicvaluechanged', listener);
  return async () => {
    chr.removeEventListener('characteristicvaluechanged', listener);
    await chr.stopNotifications();
  };
}

async function openStream(server, onLine) {
  const nus = await serviceOrNull(server, UUID.nus);
  if (nus) {
    const stop = await subscribe(await nus.getCharacteristic(UUID.nusTx), reassembleLines(onLine));
    return { write: await nus.getCharacteristic(UUID.nusRx), transport: 'Nordic UART', stop };
  }
  const hm10 = await serviceOrNull(server, UUID.hm10);
  if (!hm10) return null;
  const chr = await hm10.getCharacteristic(UUID.hm10Chr);
  const stop = await subscribe(chr, reassembleLines(onLine));
  return { write: chr, transport: 'HM-10', stop };
}

async function openFrames(server, uuid, onFrame) {
  const svc = await serviceOrNull(server, UUID.skyblip);
  if (!svc) return null;
  const chr = await svc.getCharacteristic(uuid);
  const stop = await subscribe(chr, event => {
    const bytes = bytesOf(event);
    let frame;
    try {
      frame = decodeFrame(bytes);
    } catch (error) {
      return onFrame(null, bytes, error);
    }
    onFrame(frame, bytes);
  });
  return { chr, stop };
}

const openConfig = (server, onReply) => openFrames(server, UUID.config, onReply);
const openLog = (server, onLog) => openFrames(server, UUID.log, onLog);

async function openSmp(server, onSmp) {
  const svc = await serviceOrNull(server, UUID.smp);
  if (!svc) return null;
  const chr = await svc.getCharacteristic(UUID.smpChr);
  await chr.startNotifications();
  chr.addEventListener('characteristicvaluechanged', event => onSmp(bytesOf(event)));
  return chr;
}

const DEVICE_FILTERS = [
  { services: [UUID.nus] },
  { services: [UUID.hm10] },
  { namePrefix: 'skyBlip' },
  { namePrefix: 'SoftRF' },
];
const OPTIONAL_SERVICES = [UUID.nus, UUID.hm10, UUID.skyblip, UUID.smp];

// INFO: fc 11nov26 names narrow the browser's chooser to the devices this browser already met
export function requestDevice({ names = [], namePrefix = null } = {}) {
  if (!hasWebBluetooth()) throw new Error('This browser has no Web Bluetooth: use Chrome or Edge.');
  const filters = namePrefix ? [{ namePrefix }] : names.length > 0 ? names.map(name => ({ name })) : DEVICE_FILTERS;
  return navigator.bluetooth.requestDevice({ filters, optionalServices: OPTIONAL_SERVICES });
}

export async function permittedDevices() {
  if (!hasWebBluetooth() || typeof navigator.bluetooth.getDevices !== 'function') return [];
  return navigator.bluetooth.getDevices();
}

const links = new Map();
const taps = new Map();

// INFO: fc 11nov26 one GATT link per device, shared by every client of it, so a Go and a Bravo stay connected side by side
function linkOf(device) {
  let link = links.get(device);
  if (!link) {
    link = openLink(device, () => {
      if (links.get(device) !== link) return false;
      links.delete(device);
      return true;
    });
    links.set(device, link);
    link.catch(() => links.delete(device));
  }
  return link;
}

async function openLink(device, forget) {
  const server = await device.gatt.connect();
  const link = { device, closed: false, server, clients: new Set(), exclusive: gattQueue(), tapped: null, stream: null, config: null, log: null, smp: null };
  device.addEventListener('gattserverdisconnected', () => {
    if (!forget()) return;
    for (const client of link.clients) client.onClose?.(device.name);
    link.clients.clear();
    overheard(link, { event: 'close' });
    link.closed = true;
  }, { once: true });
  overheard(link, { event: 'open' });
  return link;
}

const CALLBACKS = { stream: 'onLine', config: 'onReply', log: 'onLog', smp: 'onSmp' };
const HEARD = {
  stream: line => ({ line }),
  config: (frame, bytes, error) => ({ frame, bytes, error }),
  log: (frame, bytes, error) => ({ frame, bytes, error }),
  smp: bytes => ({ bytes }),
};

function open(link, endpoint, opener) {
  link[endpoint] ??= opener(link.server, (...args) => {
    overheard(link, { endpoint, direction: 'in', ...HEARD[endpoint](...args) });
    for (const client of link.clients) client[CALLBACKS[endpoint]]?.(...args);
  }).catch(error => {
    link[endpoint] = null;
    throw error;
  });
  return link[endpoint];
}

// INFO: fc 14nov26 a characteristic can keep the listener of a link that closed, which must not reach a tap twice
function overheard(link, entry) {
  if (link.closed) return;
  for (const listener of taps.get(link.device) ?? []) {
    try {
      listener(entry);
    } catch (error) {
      // INFO: fc 14nov26 a tap only draws, so its failure is logged and never fails the write or the client that made the frame
      console.error('A tap failed on a frame, and the link carried on:', error);
    }
  }
}

const TAPPED = { stream: openStream, log: openLog };

// INFO: fc 14nov26 a tap opens the two endpoints no page client asks for, queued behind the writes so it never collides with an upload
function openTapped(link) {
  link.tapped ??= link.exclusive(async () => {
    for (const [endpoint, opener] of Object.entries(TAPPED)) {
      try {
        await open(link, endpoint, opener);
      } catch (error) {
        overheard(link, { event: 'failed', endpoint, error });
      }
    }
  });
  return link.tapped;
}

// INFO: fc 14nov26 once the last tap lets go, the endpoints only taps wanted stop notifying, so NMEA takes nothing from an install
function closeTapped(link) {
  link.tapped = null;
  return link.exclusive(async () => {
    for (const endpoint of Object.keys(TAPPED)) {
      const wanted = taps.has(link.device) || [...link.clients].some(client => client[CALLBACKS[endpoint]]);
      const opened = link[endpoint];
      if (wanted || link.closed || !opened) continue;
      link[endpoint] = null;
      await (await opened)?.stop();
    }
  });
}

// INFO: fc 14nov26 a tap outlives the links of its device, so it hears a reconnect from its first write
export function tap(device, listener) {
  if (!taps.has(device)) taps.set(device, new Set());
  taps.get(device).add(listener);
  links.get(device)?.then(openTapped, error => listener({ event: 'failed', error }));
  return () => {
    const listeners = taps.get(device);
    listeners?.delete(listener);
    if (listeners?.size !== 0) return;
    taps.delete(device);
    if (device.gatt.connected) {
      links.get(device)?.then(closeTapped).catch(error => console.warn('The terminal could not stop the endpoints it opened:', error.message));
    }
  };
}

export async function connect({ device = null, onLine, onReply, onLog, onSmp, onClose }) {
  device ??= await requestDevice();
  const link = await linkOf(device);
  const client = { onLine, onReply, onLog, onSmp, onClose };
  link.clients.add(client);

  let stream, config, log, smp;
  try {
    stream = onLine ? await open(link, 'stream', openStream) : null;
    config = await open(link, 'config', openConfig);
    log = onLog ? await open(link, 'log', openLog) : null;
    smp = onSmp ? await open(link, 'smp', openSmp) : null;
  } catch (error) {
    link.clients.delete(client);
    throw error;
  }
  if (!stream && !config) {
    link.clients.delete(client);
    throw new Error('No serial or config service on this device.');
  }
  if (taps.has(device)) openTapped(link);
  const { exclusive } = link;
  const sent = (endpoint, bytes) => overheard(link, { endpoint, direction: 'out', bytes });
  const written = async (endpoint, write) => {
    try {
      return await write();
    } catch (error) {
      overheard(link, { event: 'failed', endpoint, error });
      throw error;
    }
  };

  return {
    device,
    name: device.name || '(unnamed)',
    transport: stream ? stream.transport : 'none',
    hasStream: Boolean(stream),
    hasConfig: Boolean(config),
    hasLog: Boolean(log),
    hasSmp: Boolean(smp),

    async sendLine(text) {
      if (!stream) throw new Error('This device has no serial endpoint.');
      const bytes = encoder.encode(nmeaSentence(text) + '\r\n');
      sent('stream', bytes);
      for (const chunk of chunks(bytes)) {
        await exclusive(() => written('stream', () => stream.write.writeValueWithoutResponse(chunk)));
      }
    },

    async sendConfig(fields) {
      if (!config) throw new Error('This device has no config endpoint.');
      const bytes = encodeFrame(fields);
      await exclusive(() => {
        sent('config', bytes);
        return written('config', () => config.chr.writeValueWithResponse(bytes));
      });
    },

    async sendLog(fields) {
      if (!log) throw new Error('This device has no log endpoint.');
      const bytes = encodeFrame(fields);
      await exclusive(() => {
        sent('log', bytes);
        return written('log', () => log.chr.writeValueWithResponse(bytes));
      });
    },

    async sendSmp(packet, writeBytes = GATT_WRITE_BYTES) {
      if (!smp) throw new Error('This device has no SMP service.');
      sent('smp', packet);
      for (const chunk of chunks(packet, writeBytes)) {
        await exclusive(() => written('smp', () => smp.writeValueWithoutResponse(chunk)));
      }
    },

    release() {
      link.clients.delete(client);
      if (link.clients.size === 0 && device.gatt.connected) device.gatt.disconnect();
    },

    disconnect() {
      if (device.gatt.connected) device.gatt.disconnect();
    },
  };
}
