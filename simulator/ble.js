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
const decoder = new TextDecoder();

export function nmeaChecksum(body) {
  let sum = 0;
  for (const ch of encoder.encode(body)) sum ^= ch;
  return sum.toString(16).toUpperCase().padStart(2, '0');
}

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
  let held = '';
  return event => {
    held += decoder.decode(event.target.value);
    const lines = held.split(/\r?\n/);
    held = lines.pop();
    for (const line of lines) if (line) onLine(line);
  };
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

async function openConfig(server, onReply) {
  const svc = await serviceOrNull(server, UUID.skyblip);
  if (!svc) return null;
  const chr = await svc.getCharacteristic(UUID.config);
  await chr.startNotifications();
  chr.addEventListener('characteristicvaluechanged', event => {
    const text = decoder.decode(event.target.value);
    try {
      onReply(JSON.parse(text), text);
    } catch {
      onReply(null, text);
    }
  });
  return chr;
}

async function openSmp(server, onSmp) {
  const svc = await serviceOrNull(server, UUID.smp);
  if (!svc) return null;
  const chr = await svc.getCharacteristic(UUID.smpChr);
  await chr.startNotifications();
  chr.addEventListener('characteristicvaluechanged', event => onSmp(bytesOf(event)));
  return chr;
}

export async function connect({ onLine, onReply, onSmp, onClose }) {
  if (!hasWebBluetooth()) throw new Error('This browser has no Web Bluetooth: use Chrome or Edge.');
  const device = await navigator.bluetooth.requestDevice({
    filters: [
      { services: [UUID.nus] },
      { services: [UUID.hm10] },
      { namePrefix: 'skyBlip' },
      { namePrefix: 'SoftRF' },
    ],
    optionalServices: [UUID.nus, UUID.hm10, UUID.skyblip, UUID.smp],
  });
  const server = await device.gatt.connect();
  device.addEventListener('gattserverdisconnected', () => onClose(device.name));

  const stream = onLine ? await openStream(server, onLine) : null;
  const config = await openConfig(server, onReply);
  const smp = onSmp ? await openSmp(server, onSmp) : null;
  if (!stream && !config) throw new Error('No serial or config service on this device.');
  const exclusive = gattQueue();

  return {
    name: device.name || '(unnamed)',
    transport: stream ? stream.transport : 'none',
    hasStream: Boolean(stream),
    hasConfig: Boolean(config),
    hasSmp: Boolean(smp),

    async sendLine(text) {
      if (!stream) throw new Error('This device has no serial endpoint.');
      for (const chunk of chunks(encoder.encode(nmeaSentence(text) + '\r\n'))) {
        await exclusive(() => stream.write.writeValueWithoutResponse(chunk));
      }
    },

    // INFO: fc 19sep26 One write is one command: a truncated "set" applies the fields that survived.
    async sendConfig(command) {
      if (!config) throw new Error('This device has no config endpoint.');
      const bytes = encoder.encode(JSON.stringify(command));
      await exclusive(() => config.writeValueWithResponse(bytes));
    },

    async sendSmp(packet) {
      if (!smp) throw new Error('This device has no SMP service.');
      for (const chunk of chunks(packet)) {
        await exclusive(() => smp.writeValueWithoutResponse(chunk));
      }
    },

    disconnect() {
      if (device.gatt.connected) device.gatt.disconnect();
    },
  };
}
