(() => {
  const CONFIG_SERVICE = '69c21301-0187-4204-a91d-02eb8858b440';
  const CONFIG = '69c21302-0187-4204-a91d-02eb8858b440';
  const SMP_SERVICE = '8d53dc1d-1db7-4cd3-868b-8a527460aa84';
  const SMP = 'da2e7828-fbce-4e01-ae9e-261174997c48';
  const link = () => import('#skyblip/link');
  const SMALL_MTU = { command: 'acknowledgement', acknowledge: false, reason: 'small_mtu' };

  const unit = {
    stored: { aircraft_type: 4, alarm_enabled: true, alarm_volume: 'soft', units: 'metric', callsign: 'F-CABC', own_icao: 0 },
    staged: null,
    capture: null,
    mtu: 247,
    product: 'skyblip_go',
    address: 3,
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
      if (command === 'update') return this.reply({ command, image: 'confirmed', swap_powered: true, imu: 'none', downgrade: true, key: 'a1a1a1a1', ...(this.product === 'skyblip_go' ? {} : { product: this.product }) });
      if (command === 'get') return this.reply({ command: 'config', version: 1, ...(this.address === null ? {} : { address: this.address }), address_mapping_table: 58, ...this.stored });
      if (command === 'defaults') return this.reply({ command, aircraft_type: 1, alarm_enabled: true, alarm_volume: 'loud', units: 'nautical', callsign: '', own_icao: 0 });
      if (command === 'set') {
        this.staged = fields;
        return this.reply({ command: 'acknowledgement', acknowledge: false, pending: true, reason: 'confirm' });
      }
      if (command === 'capture' && this.product === 'skyblip_bravo') {
        this.capture = fields.profile;
        return this.reply({ command: 'acknowledgement', acknowledge: false, pending: true, reason: 'confirm_capture' });
      }
      return this.reply({ command: 'acknowledgement', acknowledge: false, reason: 'unknown_command' });
    },

    press() {
      if (this.capture) return this.reply({ command: 'acknowledgement', acknowledge: true, reason: 'capture' });
      Object.assign(this.stored, this.staged);
      return this.reply({ command: 'acknowledgement', acknowledge: true });
    },

    refuse() {
      this.staged = null;
      return this.reply({ command: 'acknowledgement', acknowledge: false, reason: 'cancelled' });
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
      requestDevice: async () => {
        const closed = [];
        const gatt = {
          connected: true,
          connect: async () => server,
          disconnect: () => {
            gatt.connected = false;
            unit.listeners = [];
            setTimeout(() => closed.forEach(listener => listener()));
          },
        };
        return {
          name: 'skyBlip Go 00003',
          addEventListener: (type, listener) => type === 'gattserverdisconnected' && closed.push(listener),
          gatt,
        };
      },
    },
  });
})();
