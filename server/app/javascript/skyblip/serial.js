const USB_BRIDGES = { 0x1a86: 'CH340', 0x0403: 'FTDI', 0x10c4: 'CP210x', 0x303a: 'ESP32', 0x239a: 'Adafruit' };

export function hasWebSerial() {
  return Boolean(globalThis.navigator && navigator.serial);
}

export async function openSerial({ baudRate, onBytes, onClose }) {
  if (!hasWebSerial()) throw new Error('This browser has no Web Serial: use Chrome or Edge on a computer.');
  const port = await navigator.serial.requestPort();
  await port.open({ baudRate });
  const reader = port.readable.getReader();
  let closing = false;
  read(port, reader, onBytes).then(
    () => onClose(null),
    error => onClose(closing ? null : error),
  );
  return {
    name: nameOf(port.getInfo(), baudRate),
    async close() {
      closing = true;
      await reader.cancel();
    },
  };
}

async function read(port, reader, onBytes) {
  try {
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      onBytes(value);
    }
  } finally {
    reader.releaseLock();
    await port.close();
  }
}

function nameOf({ usbVendorId, usbProductId }, baudRate) {
  if (usbVendorId === undefined) return `serial port at ${baudRate} baud`;
  const bridge = USB_BRIDGES[usbVendorId] ?? usbVendorId.toString(16).padStart(4, '0');
  return `USB ${bridge} ${usbProductId.toString(16).padStart(4, '0')} at ${baudRate} baud`;
}
