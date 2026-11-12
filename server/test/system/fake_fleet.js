(() => {
  const UUID = {
    skyblip: '69c21301-0187-4204-a91d-02eb8858b440',
    config: '69c21302-0187-4204-a91d-02eb8858b440',
    smp: '8d53dc1d-1db7-4cd3-868b-8a527460aa84',
    smpChr: 'da2e7828-fbce-4e01-ae9e-261174997c48',
  };
  const PKCS8_ED25519 = '302e020100300506032b657004220420';
  const SMP_HEADER = 8;
  const DEFAULTS = { aircraft_type: 1, alarm_enabled: true, alarm_volume: 'loud', units: 'nautical', callsign: '', own_icao: 0 };
  const encoder = new TextEncoder();
  const modules = () => Promise.all([import('#skyblip/link'), import('#skyblip/cbor'), import('#skyblip/image')]);
  const later = (fn, ms = 0) => setTimeout(fn, ms);
  const bytes = (...hex) => Uint8Array.from(hex.join('').match(/../g), pair => parseInt(pair, 16));
  const hex = buffer => [...new Uint8Array(buffer)].map(byte => byte.toString(16).padStart(2, '0')).join('');
  const join = (...parts) => {
    const out = new Uint8Array(parts.reduce((sum, part) => sum + part.length, 0));
    let at = 0;
    for (const part of parts) {
      out.set(part, at);
      at += part.length;
    }
    return out;
  };
  const label = purpose => encoder.encode(`skyblip:${purpose}:v1`);

  class Unit {
    constructor(options) {
      Object.assign(this, {
        name: 'skyBlip Go 00012A',
        product: null,
        address: 0x00012a,
        running: '0.1.0+1000',
        key: '212602F2',
        downgrade: true,
        imu: '318def511fd8eb76',
        image: 'confirmed',
        from: null,
        to: null,
        flight: 'ground',
        battery: 82,
        charging: false,
        stored: { aircraft_type: 3, alarm_enabled: true, alarm_volume: 'soft', units: 'nautical', callsign: 'F-JXYZ', own_icao: 0 },
        mtu: 247,
        pressAfterMs: null,
        uploadDelayMs: 0,
        rebootMs: 1500,
        probationMs: 1500,
        vectors: null,
      }, options);
      this.connected = false;
      this.rebooting = false;
      this.pending = null;
      this.refused = false;
      this.windowOpen = false;
      this.listeners = { config: [], smp: [] };
      this.closed = [];
      this.held = new Uint8Array(0);
      this.device = this.#device();
    }

    #device() {
      const unit = this;
      const characteristic = (kind, write) => ({
        startNotifications: async () => {},
        addEventListener: (type, listener) => unit.listeners[kind].push(listener),
        writeValueWithResponse: write,
        writeValueWithoutResponse: write,
      });
      const services = {
        [UUID.skyblip]: { [UUID.config]: characteristic('config', raw => unit.onConfig(raw)) },
        [UUID.smp]: { [UUID.smpChr]: characteristic('smp', raw => unit.onSmp(raw)) },
      };
      const server = {
        getPrimaryService: async uuid => {
          const service = services[uuid];
          if (!service) throw new Error(`no service ${uuid}`);
          return { getCharacteristic: async id => service[id] };
        },
      };
      return {
        id: unit.name,
        get name() { return unit.name; },
        addEventListener: (type, listener) => type === 'gattserverdisconnected' && unit.closed.push(listener),
        gatt: {
          get connected() { return unit.connected; },
          connect: async () => {
            if (unit.rebooting) throw new Error('Connection attempt failed.');
            unit.connected = true;
            later(() => unit.linkUp());
            return server;
          },
          disconnect: () => unit.drop(),
        },
      };
    }

    linkUp() {
      if (this.image !== 'confirmed') this.sendUpdate();
    }

    drop() {
      if (!this.connected) return;
      this.connected = false;
      this.pending = null;
      this.refused = false;
      this.windowOpen = false;
      this.listeners = { config: [], smp: [] };
      const closed = this.closed.splice(0);
      later(() => closed.forEach(listener => listener()));
    }

    async reply(fields) {
      const [{ encodeFrame }] = await modules();
      const whole = encodeFrame(fields);
      const frame = whole.length <= this.mtu - 3 ? whole : encodeFrame({ command: 'acknowledgement', acknowledge: false, reason: 'small_mtu' });
      const value = new DataView(frame.buffer);
      later(() => this.connected && this.listeners.config.forEach(listener => listener({ target: { value } })));
    }

    acknowledge(acknowledge, reason, pending = undefined) {
      return this.reply({ command: 'acknowledgement', acknowledge, pending, reason });
    }

    status() {
      return this.reply({
        command: 'status', flight: this.flight, upload: this.windowOpen, battery_percent: this.battery, charging: this.charging,
        power_level: this.battery < 10 ? 'critical' : this.battery < 20 ? 'low' : 'normal', went_dark_flat: false,
      });
    }

    sendUpdate() {
      const frame = { command: 'update', image: this.image, imu: this.imu ? 'held' : 'none', swap_powered: this.battery >= 10 };
      if (this.imu) frame.imu_digest = this.imu;
      if (this.image !== 'confirmed') Object.assign(frame, { from: this.from, to: this.to });
      if (this.downgrade) frame.downgrade = true;
      if (this.key) frame.key = this.key;
      if (this.product) frame.product = this.product;
      return this.reply(frame);
    }

    config() {
      const address = this.address === null ? {} : { address: this.address };
      const stored = this.product === 'skyblip_bravo'
        ? { alarm_enabled: this.stored.alarm_enabled, alarm_volume: this.stored.alarm_volume, own_icao: this.stored.own_icao }
        : this.stored;
      return this.reply({ command: 'config', version: 1, ...address, address_mapping_table: 58, ...stored });
    }

    ask(task, reason) {
      if (this.refused) return this.acknowledge(false, 'refused_on_device');
      if (this.flight === 'airborne' && task !== 'register') return this.acknowledge(false, 'in_flight');
      this.pending = task;
      this.acknowledge(false, reason, true);
      if (this.pressAfterMs !== null) later(() => this.pending === task && this.press(), this.pressAfterMs);
    }

    async onConfig(raw) {
      const [{ decodeFrame }] = await modules();
      const { command, ...fields } = decodeFrame(raw);
      if (command === 'status') return this.status();
      if (command === 'update') return this.sendUpdate();
      if (command === 'get') return this.config();
      if (command === 'defaults') return this.reply({ command: 'defaults', ...DEFAULTS });
      if (command === 'set') {
        this.staged = fields;
        return this.ask('set', 'confirm');
      }
      if (command === 'dfu') {
        this.approved = fields.version;
        return this.ask('dfu', 'confirm_dfu');
      }
      if (command === 'recovery') return this.ask('recovery', 'confirm_recovery');
      if (command === 'capture' && this.product === 'skyblip_bravo') return this.orderCapture(fields.profile);
      if (command === 'challenge') {
        this.nonce = hex(crypto.getRandomValues(new Uint8Array(16)));
        return this.reply({ command: 'challenge', nonce: this.nonce, server_key_id: this.vectors.server_key_id });
      }
      if (command === 'register') {
        if (!(await this.verifies(fields.signature, join(label('register'), bytes(this.nonce), fields.server_nonce)))) return this.acknowledge(false, 'bad_signature');
        this.serverNonce = hex(fields.server_nonce);
        return this.ask('register', 'confirm_register');
      }
      if (command === 'unlock') return this.unlock(fields);
      return this.acknowledge(false, 'unknown_command');
    }

    // INFO: fc 12nov26 capture_request.cpp's checks after the prompt guards and the flight
    orderCapture(profile) {
      const stop = profile === 'stop';
      if (!this.refused && this.flight !== 'airborne' && Boolean(this.capturing) !== stop) return this.acknowledge(false, stop ? 'not_capturing' : 'capturing');
      this.capture = profile;
      return this.ask('capture', 'confirm_capture');
    }

    async press() {
      const task = this.pending;
      this.pending = null;
      if (task === 'capture') {
        this.capturing = this.capture !== 'stop';
        return this.acknowledge(true, 'capture');
      }
      if (task === 'set') {
        Object.assign(this.stored, this.staged);
        return this.reply({ command: 'acknowledgement', acknowledge: true });
      }
      if (task === 'dfu') {
        this.windowOpen = true;
        return this.acknowledge(true, 'dfu');
      }
      if (task === 'recovery') {
        this.acknowledge(true, 'recovery');
        return later(() => this.drop(), 50);
      }
      if (task === 'register') return this.register();
    }

    refuse() {
      if (!this.pending) return;
      this.pending = null;
      this.refused = true;
      this.acknowledge(false, 'cancelled');
    }

    takeOff() {
      this.flight = 'airborne';
      this.status();
    }

    land() {
      this.flight = 'ground';
      this.status();
    }

    async register() {
      const { device_id, unit_public_key, unit_seed, server_key_id, release, version } = this.vectors;
      const key = await crypto.subtle.importKey('pkcs8', bytes(PKCS8_ED25519, unit_seed), { name: 'Ed25519' }, false, ['sign']);
      const message = join(label('registration'), bytes(device_id, unit_public_key, this.nonce, this.serverNonce, server_key_id),
        Uint8Array.of(release ? 1 : 0), encoder.encode(version));
      const signature = hex(await crypto.subtle.sign('Ed25519', key, message));
      this.reply({ command: 'registration', device_id, public_key: unit_public_key, version, server_key_id, release, signature });
    }

    async unlock(fields) {
      const serial = new Uint8Array([fields.serial >> 8, fields.serial & 0xff]);
      const message = join(label('unlock'), bytes(this.vectors.device_id, this.vectors.unit_public_key), serial, bytes(this.nonce));
      if (!(await this.verifies(fields.signature, message))) return this.acknowledge(false, 'bad_signature');
      this.address = fields.serial;
      this.name = `skyBlip Go ${fields.serial.toString(16).toUpperCase().padStart(6, '0')}`;
      return this.acknowledge(true, 'unlock');
    }

    async verifies(signature, message) {
      const key = await crypto.subtle.importKey('raw', bytes(this.vectors.server_public_key), { name: 'Ed25519' }, false, ['verify']);
      return crypto.subtle.verify('Ed25519', key, signature, message);
    }

    onSmp(raw) {
      const joined = new Uint8Array(this.held.length + raw.length);
      joined.set(this.held);
      joined.set(raw, this.held.length);
      this.held = joined;
      if (this.held.length < SMP_HEADER) return;
      const end = SMP_HEADER + ((this.held[2] << 8) | this.held[3]);
      if (this.held.length < end) return;
      const request = this.held.slice(0, end);
      this.held = this.held.slice(end);
      this.answerSmp(request);
    }

    async answerSmp(request) {
      const [, { decode, encode }, { readImage, versionText }] = await modules();
      const group = (request[4] << 8) | request[5];
      const id = request[7];
      const body = decode(request.subarray(SMP_HEADER));
      let answer;
      if (group === 0 && id === 0) answer = { r: body.d };
      else if (group === 0 && id === 6) answer = { buf_size: 2475, buf_count: 4 };
      else if (group === 1 && id === 0) answer = { images: [{ slot: 0, version: this.running.replace('+', '.'), active: true, confirmed: true }] };
      else if (group === 1 && id === 1) answer = this.upload(body, readImage, versionText);
      else answer = { rc: 8 };
      const payload = encode(answer);
      const packet = new Uint8Array(SMP_HEADER + payload.length);
      packet.set(request.subarray(0, SMP_HEADER));
      packet[0] = (request[0] & ~0x07) | ((request[0] & 0x07) + 1);
      packet[2] = payload.length >> 8;
      packet[3] = payload.length & 0xff;
      packet.set(payload, SMP_HEADER);
      later(() => {
        const fragment = this.mtu - 3;
        for (let at = 0; at < packet.length && this.connected; at += fragment) {
          const value = new DataView(packet.buffer, at, Math.min(fragment, packet.length - at));
          this.listeners.smp.forEach(listener => listener({ target: { value } }));
        }
      }, id === 1 && group === 1 ? this.uploadDelayMs : 0);
    }

    upload({ off, len, data }, readImage, versionText) {
      if (!this.windowOpen) return { rc: 11 };
      if (off === 0) {
        this.slot = new Uint8Array(len);
        this.reached = 0;
      }
      if (off !== this.reached) return { off: this.reached };
      this.slot.set(data, off);
      this.reached += data.length;
      if (this.reached === this.slot.length) later(() => this.installed(readImage, versionText));
      return { off: this.reached };
    }

    installed(readImage, versionText) {
      this.windowOpen = false;
      const image = readImage(this.slot);
      if (!image || versionText(image.version) !== this.approved) return this.acknowledge(false, 'not_approved');
      this.acknowledge(true, 'install');
      later(() => this.reboot(this.approved), 50);
    }

    reboot(version) {
      this.drop();
      this.rebooting = true;
      later(() => {
        this.rebooting = false;
        this.from = this.running;
        this.to = version;
        this.running = version;
        this.image = 'probation';
        later(() => {
          this.image = 'confirmed';
          if (this.connected) this.sendUpdate();
        }, this.probationMs);
      }, this.rebootMs);
    }
  }

  const fleet = {
    units: [],
    pick: null,
    asked: [],
    add(options) {
      const unit = new Unit(options);
      this.units.push(unit);
      return unit;
    },
    unit(name) {
      return this.units.find(unit => unit.name === name || unit.name.endsWith(name));
    },
  };

  const matches = (unit, filters) => filters.some(({ name, namePrefix, services }) =>
    (name && unit.name === name) || (namePrefix && unit.name.startsWith(namePrefix)) || Boolean(services));

  window.fakeFleet = fleet;
  Object.defineProperty(navigator, 'bluetooth', {
    configurable: true,
    value: {
      requestDevice: async ({ filters }) => {
        fleet.asked.push(filters);
        const unit = (fleet.pick && fleet.unit(fleet.pick)) || fleet.units.find(each => matches(each, filters));
        fleet.pick = null;
        if (!unit || !matches(unit, filters)) throw Object.assign(new Error('User cancelled the requestDevice() chooser.'), { name: 'NotFoundError' });
        return unit.device;
      },
    },
  });
})();
