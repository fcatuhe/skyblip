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

async function openStream(server, onLine) {
  const nus = await serviceOrNull(server, UUID.nus);
  if (nus) {
    const tx = await nus.getCharacteristic(UUID.nusTx);
    await tx.startNotifications();
    tx.addEventListener('characteristicvaluechanged', reassembleLines(onLine));
    return { write: await nus.getCharacteristic(UUID.nusRx), transport: 'Nordic UART' };
  }
  const hm10 = await serviceOrNull(server, UUID.hm10);
  if (!hm10) return null;
  const chr = await hm10.getCharacteristic(UUID.hm10Chr);
  await chr.startNotifications();
  chr.addEventListener('characteristicvaluechanged', reassembleLines(onLine));
  return { write: chr, transport: 'HM-10' };
}

async function openFrames(server, uuid, onFrame) {
  const svc = await serviceOrNull(server, UUID.skyblip);
  if (!svc) return null;
  const chr = await svc.getCharacteristic(uuid);
  await chr.startNotifications();
  chr.addEventListener('characteristicvaluechanged', event => {
    const bytes = bytesOf(event);
    let frame;
    try {
      frame = decodeFrame(bytes);
    } catch (error) {
      return onFrame(null, bytes, error);
    }
    onFrame(frame, bytes);
  });
  return chr;
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
  const link = { server, clients: new Set(), exclusive: gattQueue(), stream: null, config: null, log: null, smp: null };
  device.addEventListener('gattserverdisconnected', () => {
    if (!forget()) return;
    for (const client of link.clients) client.onClose?.(device.name);
    link.clients.clear();
  }, { once: true });
  return link;
}

const CALLBACKS = { stream: 'onLine', config: 'onReply', log: 'onLog', smp: 'onSmp' };

function open(link, endpoint, opener) {
  link[endpoint] ??= opener(link.server, (...args) => {
    for (const client of link.clients) client[CALLBACKS[endpoint]]?.(...args);
  }).catch(error => {
    link[endpoint] = null;
    throw error;
  });
  return link[endpoint];
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
  const { exclusive } = link;

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
      for (const chunk of chunks(encoder.encode(nmeaSentence(text) + '\r\n'))) {
        await exclusive(() => stream.write.writeValueWithoutResponse(chunk));
      }
    },

    async sendConfig(fields) {
      if (!config) throw new Error('This device has no config endpoint.');
      const bytes = encodeFrame(fields);
      await exclusive(() => config.writeValueWithResponse(bytes));
    },

    async sendLog(fields) {
      if (!log) throw new Error('This device has no log endpoint.');
      const bytes = encodeFrame(fields);
      await exclusive(() => log.writeValueWithResponse(bytes));
    },

    async sendSmp(packet, writeBytes = GATT_WRITE_BYTES) {
      if (!smp) throw new Error('This device has no SMP service.');
      for (const chunk of chunks(packet, writeBytes)) {
        await exclusive(() => smp.writeValueWithoutResponse(chunk));
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
