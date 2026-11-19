(() => {
  const encoder = new TextEncoder();
  const hex = bytes => [...bytes].map(byte => byte.toString(16).padStart(2, '0')).join('');
  const later = (fn, ms = 0) => setTimeout(fn, ms);
  const named = (name, message) => Object.assign(new Error(message), { name });
  const MAGIC = encoder.encode('skyblip:trust:v1');
  // INFO: fc 17nov26 the factory INFO_UF2.TXT Board-ID of each board, firmware/boards/*/factory
  const BOARDS = { go: 'nRF52840-TEcho-v1', bravo: 'nRF52840-MeshTracker-X1-v1' };

  const varint = value => {
    const bytes = [];
    let rest = value >>> 0;
    while (rest > 0x7f) {
      bytes.push((rest & 0x7f) | 0x80);
      rest >>>= 7;
    }
    bytes.push(rest);
    return bytes;
  };
  const field = (number, bytes) => [...varint((number << 3) | 2), ...varint(bytes.length), ...bytes];
  const number = (key, value) => [...varint(key << 3), ...varint(value)];
  const frame = payload => Uint8Array.of(0x94, 0xc3, payload.length >> 8, payload.length & 0xff, ...payload);

  // INFO: fc 17nov26 what the bootloader takes from a UF2: the blocks, of which we read back the trust record's key and label
  function trustOf(bytes) {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    for (let at = 0; at + 512 <= bytes.length; at += 512) {
      const size = view.getUint32(at + 16, true);
      const data = bytes.subarray(at + 32, at + 32 + size);
      if (MAGIC.every((byte, index) => data[index] === byte)) {
        const length = data[48];
        return { id: hex(data.subarray(16, 24)), label: new TextDecoder().decode(data.subarray(49, 49 + length)) };
      }
    }
    return null;
  }

  class Drive {
    constructor(device) {
      this.device = device;
      this.files = new Map([['INFO_UF2.TXT', `UF2 Bootloader 0.6.1\nModel: ${device.product === 'go' ? 'LilyGo T-Echo' : 'Seeed MeshTracker X1'}\nBoard-ID: ${BOARDS[device.product]}\n`]]);
      this.name = device.product === 'go' ? 'TECHOBOOT' : 'X1';
    }

    async getFileHandle(name, { create = false } = {}) {
      if (!this.files.has(name) && !create) throw named('NotFoundError', 'A requested file or directory could not be found.');
      const drive = this;
      return {
        getFile: async () => new Blob([drive.files.get(name)]),
        createWritable: async () => {
          let written = new Uint8Array();
          return {
            write: async bytes => (written = Uint8Array.from([...written, ...bytes])),
            abort: async () => {},
            close: async () => {
              drive.files.set(name, written);
              drive.device.flash(written);
            },
          };
        },
      };
    }
  }

  class Device {
    constructor(options) {
      Object.assign(this, { product: 'go', nodeNum: 0x9a3f2a, hwModel: 7, mode: 'meshtastic', unit: null, flashDelayMs: 0 }, options);
    }

    port() {
      const device = this;
      let push;
      const readable = new ReadableStream({ start: controller => (push = controller) });
      let held = [];
      const writable = new WritableStream({
        write(bytes) {
          held.push(...bytes);
          for (;;) {
            const start = held.findIndex((byte, index) => byte === 0x94 && held[index + 1] === 0xc3);
            if (start < 0 || held.length < start + 4) return;
            const length = (held[start + 2] << 8) | held[start + 3];
            if (held.length < start + 4 + length) return;
            const payload = held.slice(start + 4, start + 4 + length);
            held = held.slice(start + 4 + length);
            device.hear(payload, bytes => push.enqueue(bytes));
          }
        },
      });
      // INFO: fc 18nov26 the USB ids installer.js reads its firmware from, the vendor alone for a board it does not list
      const ids = { meshtastic: 0x4405, softrf: 0x8029, bootloader: 0x0029 }[device.mode] ?? 0x1234;
      return {
        getInfo: () => ({ usbVendorId: device.product === 'go' ? 0x239a : 0x2886, usbProductId: device.product === 'go' ? ids : 0x1234 }),
        open: async () => {},
        close: async () => {},
        readable,
        writable,
      };
    }

    hear(payload, push) {
      if (this.mode !== 'meshtastic') return;
      const key = payload[0];
      if (key === 0x18) {
        push(encoder.encode('INFO  | ??:??:?? 2 [Router] Booting\r\n'));
        push(frame(field(3, number(1, this.nodeNum))));
        push(frame(field(13, [...field(1, [...encoder.encode('2.7.15.567b8ea')]), ...number(9, this.hwModel)])));
        push(frame(number(7, 69420)));
      }
      if (key === 0x0a) {
        fakeUsb.restarts++;
        later(() => (fakeUsb.drive = new Drive(this)), 50);
      }
    }

    flash(bytes) {
      fakeUsb.drive = null;
      fakeUsb.flashed.push(bytes.length);
      const trusted = trustOf(bytes);
      fakeUsb.trusted = trusted;
      later(() => fakeFleet.add({ ...this.unit, browsers: trusted ? [trusted] : [] }), this.flashDelayMs);
    }
  }

  const fakeUsb = {
    plugged: null,
    drive: null,
    asked: [],
    restarts: 0,
    flashed: [],
    trusted: null,
    plug(options) {
      this.plugged = new Device(options);
      if (this.plugged.mode === 'bootloader') this.drive = new Drive(this.plugged);
      return this.plugged;
    },
  };

  window.fakeUsb = fakeUsb;
  Object.defineProperty(navigator, 'serial', {
    configurable: true,
    value: {
      requestPort: async ({ filters }) => {
        fakeUsb.asked.push(filters);
        const device = fakeUsb.plugged;
        if (!device || device.mode === 'skyblip') throw named('NotFoundError', 'No port selected by the user.');
        return device.port();
      },
    },
  });
  window.showDirectoryPicker = async () => {
    if (!fakeUsb.drive) throw named('AbortError', 'The user aborted a request.');
    return fakeUsb.drive;
  };
})();
