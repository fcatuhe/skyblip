(() => {
  const CONFIG_SERVICE = '69c21301-0187-4204-a91d-02eb8858b440';
  const CONFIG = '69c21302-0187-4204-a91d-02eb8858b440';
  const SMP_SERVICE = '8d53dc1d-1db7-4cd3-868b-8a527460aa84';
  const SMP = 'da2e7828-fbce-4e01-ae9e-261174997c48';
  const link = () => import('#skyblip/link');
  const SMALL_MTU = { command: 'acknowledgement', acknowledge: false, reason: 'small_mtu' };

  const unit = {
    stored: { aircraft_type: 4, alarm_enabled: true, alarm_volume: 'soft', units: 'metric', callsign: 'F-CABC' },
    staged: null,
    mtu: 247,
    listeners: [],

    async reply(fields) {
      const { encodeFrame } = await link();
      const whole = encodeFrame(fields);
      const frame = whole.length <= this.mtu - 3 ? whole : encodeFrame(SMALL_MTU);
      const value = new DataView(frame.buffer);
      setTimeout(() => this.listeners.forEach(listener => listener({ target: { value } })));
    },

    async write(raw) {
      const { decodeFrame } = await link();
      const { command, ...fields } = decodeFrame(raw);
      if (command === 'status') return this.reply({ command, flight: 'ground', upload: false, charging: false, power_level: 'normal', went_dark_flat: false });
      if (command === 'get') return this.reply({ command: 'config', version: 1, address: 3, address_mapping_table: 58, ...this.stored });
      if (command === 'defaults') return this.reply({ command, aircraft_type: 1, alarm_enabled: true, alarm_volume: 'loud', units: 'nautical', callsign: '' });
      if (command === 'set') {
        this.staged = fields;
        return this.reply({ command: 'acknowledgement', acknowledge: false, pending: true, reason: 'confirm' });
      }
      return this.reply({ command: 'acknowledgement', acknowledge: false, reason: 'unknown_command' });
    },

    press() {
      Object.assign(this.stored, this.staged);
      return this.reply({ command: 'acknowledgement', acknowledge: true });
    },
  };

  const config = {
    startNotifications: async () => {},
    addEventListener: (type, listener) => unit.listeners.push(listener),
    writeValueWithResponse: bytes => unit.write(bytes),
  };
  const smp = {
    startNotifications: async () => {},
    addEventListener: () => {},
    writeValueWithoutResponse: async () => {},
  };
  const characteristics = { [CONFIG_SERVICE]: { [CONFIG]: config }, [SMP_SERVICE]: { [SMP]: smp } };

  const server = {
    getPrimaryService: async uuid => {
      const service = characteristics[uuid];
      if (!service) throw new Error(`no service ${uuid}`);
      return { getCharacteristic: async id => service[id] ?? Promise.reject(new Error(id)) };
    },
  };

  window.fakeSkyblip = unit;
  Object.defineProperty(navigator, 'bluetooth', {
    configurable: true,
    value: {
      requestDevice: async () => ({
        name: 'skyBlip Go 00003',
        addEventListener: () => {},
        gatt: { connected: true, connect: async () => server, disconnect: () => {} },
      }),
    },
  });
})();
