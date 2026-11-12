(() => {
  const NUS = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
  const NUS_RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
  const NUS_TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';
  const encoder = new TextEncoder();
  const bytes = hex => Uint8Array.from(hex.match(/../g), pair => parseInt(pair, 16));

  const retro = {
    reader: null,
    async send(messageHex) {
      const { gdl90Crc } = await import('#skyblip/gdl90');
      const message = bytes(messageHex.replaceAll(' ', ''));
      const crc = gdl90Crc(message);
      const body = [...message, crc & 0xff, crc >> 8].flatMap(byte => (byte === 0x7e || byte === 0x7d ? [0x7d, byte ^ 0x20] : [byte]));
      this.reader.enqueue(Uint8Array.from([0x7e, ...body, 0x7e]));
    },
    port: {
      async open({ baudRate }) { retro.baudRate = baudRate; },
      async close() {},
      getInfo: () => ({ usbVendorId: 0x1a86, usbProductId: 0x7523 }),
      get readable() {
        return new ReadableStream({ start: controller => { retro.reader = controller; } });
      },
    },
  };

  const bravo = {
    listeners: [],
    connected: false,
    async send(body) {
      const { nmeaChecksum } = await import('#skyblip/nmea');
      const value = new DataView(encoder.encode(`$${body}*${nmeaChecksum(body)}\r\n`).buffer);
      this.listeners.forEach(listener => listener({ target: { value } }));
    },
  };
  const characteristic = {
    startNotifications: async () => {},
    addEventListener: (type, listener) => bravo.listeners.push(listener),
    writeValueWithoutResponse: async () => {},
  };
  const server = {
    getPrimaryService: async uuid => {
      if (uuid !== NUS) throw new Error(`no service ${uuid}`);
      return { getCharacteristic: async id => (id === NUS_TX || id === NUS_RX ? characteristic : null) };
    },
  };
  const device = {
    name: 'skyBlip Bravo 3F2A',
    addEventListener: () => {},
    gatt: {
      get connected() { return bravo.connected; },
      connect: async () => { bravo.connected = true; return server; },
      disconnect: () => { bravo.connected = false; },
    },
  };

  window.fakeReceivers = { retro, bravo };
  Object.defineProperty(navigator, 'serial', { configurable: true, value: { requestPort: async () => retro.port } });
  Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: { requestDevice: async () => device } });
})();
