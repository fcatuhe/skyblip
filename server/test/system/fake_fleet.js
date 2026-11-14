(() => {
  const UUID = {
    nus: '6e400001-b5a3-f393-e0a9-e50e24dcca9e',
    nusRx: '6e400002-b5a3-f393-e0a9-e50e24dcca9e',
    nusTx: '6e400003-b5a3-f393-e0a9-e50e24dcca9e',
    skyblip: '69c21301-0187-4204-a91d-02eb8858b440',
    config: '69c21302-0187-4204-a91d-02eb8858b440',
    log: '69c21303-0187-4204-a91d-02eb8858b440',
    smp: '8d53dc1d-1db7-4cd3-868b-8a527460aa84',
    smpChr: 'da2e7828-fbce-4e01-ae9e-261174997c48',
  };
  const PKCS8_ED25519 = '302e020100300506032b657004220420';
  const SMP_HEADER = 8;
  const RECORD_BYTES = 24;
  // INFO: fc 12nov26 log_link.cpp's chunk envelope with its store field, and the clamps it applies, as fake_log.mjs reads them
  const CHUNK_ENVELOPE_BYTES = 20;
  const MAX_RECORDS_PER_CHUNK = 19;
  const MAX_READ_CHUNKS = 8;
  const BROWSER_COMMANDS = ['browser_challenge', 'trust_browser', 'browsers', 'forget_browser'];
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
  const recordsPerChunk = payload => {
    for (let records = MAX_RECORDS_PER_CHUNK; records > 0; records--) {
      const raw = records * RECORD_BYTES;
      if (CHUNK_ENVELOPE_BYTES + (raw < 24 ? 1 : raw < 256 ? 2 : 3) + raw <= payload) return records;
    }
    return 0;
  };

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
        diagnostics: [],
        servesCaptures: null,
        evictsFlights: 3,
        browsers: null,
        installReport: null,
        protocols: null,
        failure: null,
      }, options);
      this.connected = false;
      this.rebooting = false;
      this.pending = null;
      this.refused = false;
      this.windowOpen = false;
      this.listeners = { nmea: [], config: [], log: [], smp: [] };
      this.closed = [];
      this.held = new Uint8Array(0);
      this.device = this.#device();
    }

    #device() {
      const unit = this;
      const characteristic = (kind, write) => ({
        startNotifications: async () => {},
        stopNotifications: async () => {},
        addEventListener: (type, listener) => unit.listeners[kind].push(listener),
        removeEventListener: (type, listener) => {
          unit.listeners[kind] = unit.listeners[kind].filter(each => each !== listener);
        },
        writeValueWithResponse: write,
        writeValueWithoutResponse: write,
      });
      // INFO: fc 14nov26 the unit discards what a central writes to its UART, as on_stream_write does
      const services = {
        [UUID.nus]: { [UUID.nusTx]: characteristic('nmea', null), [UUID.nusRx]: characteristic('nmea', async () => {}) },
        [UUID.skyblip]: { [UUID.config]: characteristic('config', raw => unit.onConfig(raw)), [UUID.log]: characteristic('log', raw => unit.onLog(raw)) },
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
      this.browserNonce = null;
      this.proven = null;
      this.trusted = false;
      this.listeners = { nmea: [], config: [], log: [], smp: [] };
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

    sentence(line) {
      const value = new DataView(encoder.encode(`${line}\r\n`).buffer);
      later(() => this.connected && this.listeners.nmea.forEach(listener => listener({ target: { value } })));
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
      const trustedSet = task === 'set' && this.trusted;
      if (this.flight === 'airborne' && task !== 'register' && !trustedSet) return this.acknowledge(false, 'in_flight');
      this.pending = task;
      if (this.trusted && ['set', 'dfu'].includes(task)) return this.press();
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
      if (command === 'install_report') return this.reply({ command, ...this.installReport });
      if (command === 'protocols' && this.protocols) return this.reply({ command, ...this.protocols });
      if (command === 'set') {
        this.staged = fields;
        return this.ask('set', 'confirm');
      }
      if (command === 'dfu') {
        this.approved = fields.version;
        return this.ask('dfu', 'confirm_dfu');
      }
      if (command === 'recovery') return this.ask('recovery', 'confirm_recovery');
      if (command === 'capture' && this.captures()) return this.orderCapture(fields.profile);
      if (command === 'diagnostics') return this.dump();
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
      if (command === 'attest' && this.vectors) return this.attest(fields);
      if (this.browsers && BROWSER_COMMANDS.includes(command)) return this.answerBrowser(command, fields);
      return this.acknowledge(false, 'unknown_command');
    }

    async onLog(raw) {
      const [{ decodeFrame, encodeFrame }] = await modules();
      for (const reply of this.logReplies(decodeFrame(raw))) {
        const value = new DataView(encodeFrame({ log: 'diagnostics', ...reply }).buffer);
        later(() => this.connected && this.listeners.log.forEach(listener => listener({ target: { value } })));
      }
    }

    logReplies({ command, index, session, from = 0, count = 1 }) {
      const sessions = this.diagnostics.map(each => ({ ...each, held: bytes(each.records) }));
      const refuse = reason => [{ command: 'log', acknowledge: false, reason }];
      if (command === 'list' && index === undefined) return [{ command: 'log', acknowledge: true, sessions: sessions.length }];
      if (command === 'list') {
        if (!sessions[index]) return refuse('no_session');
        const { closed = true, held } = sessions[index];
        return [{ command: 'session', index, of: sessions.length, session: sessions[index].session, records: held.length / RECORD_BYTES, closed, truncated: false }];
      }
      const entry = sessions.find(each => each.session === session);
      if (!entry) return refuse('no_session');
      const perChunk = recordsPerChunk(this.mtu - 3);
      if (perChunk === 0) return refuse('small_mtu');
      const total = entry.held.length / RECORD_BYTES;
      if (from >= total) return [{ command: 'chunk', session, from, ends_session: true, records: new Uint8Array(0) }];
      const replies = [];
      for (let at = from; at < total && replies.length < Math.min(Math.max(count, 1), MAX_READ_CHUNKS); at += perChunk) {
        const end = Math.min(at + perChunk, total);
        replies.push({ command: 'chunk', session, from: at, ends_session: end >= total, records: entry.held.slice(at * RECORD_BYTES, end * RECORD_BYTES) });
      }
      return replies;
    }

    // INFO: fc 15nov26 a Bravo always served capture, and a Go from the firmware that answers its dump group
    captures() {
      return this.servesCaptures ?? this.product === 'skyblip_bravo';
    }

    async dump() {
      await this.reply({ command: 'diagnostics', group: 'system', part: 0, more: this.captures(), uptime_s: 120, reset: 'power_on' });
      if (!this.captures()) return;
      return this.reply({
        command: 'diagnostics', group: 'capture', part: 1, more: false, armed: Boolean(this.capturing),
        ...(this.capturing ? { profile: this.capture } : {}), session: this.capturing ? 1785628800 : 0, records: this.capturing ? 960 : 0,
        records_dropped: 0, sectors: 0, keeps_s: this.capturing ? 433500 : 0, takes_sectors: 170, evicts_flights: this.evictsFlights,
        keeps_full_s: 1926, keeps_power_run_s: 433500,
      });
    }

    // INFO: fc 12nov26 capture_request.cpp's checks after the prompt guards and the flight
    orderCapture(profile) {
      const stop = profile === 'stop';
      if (!this.refused && this.flight !== 'airborne' && Boolean(this.capturing) !== stop) return this.acknowledge(false, stop ? 'not_capturing' : 'capturing');
      this.capture = profile;
      return this.ask('capture', 'confirm_capture');
    }

    // INFO: fc 13nov26 browser_link.cpp's answers, for a unit made with a browsers list
    async answerBrowser(command, fields) {
      if (command === 'browser_challenge') {
        this.proven = null;
        this.trusted = false;
        this.browserNonce = crypto.getRandomValues(new Uint8Array(16));
        return this.reply({ command, nonce: this.browserNonce });
      }
      if (command === 'trust_browser') {
        const nonce = this.browserNonce;
        this.browserNonce = null;
        if (!nonce) return this.acknowledge(false, 'no_session');
        if (encoder.encode(fields.label ?? '').length > 24) return this.acknowledge(false, 'invalid');
        const key = await crypto.subtle.importKey('raw', fields.key, { name: 'Ed25519' }, false, ['verify']);
        if (!(await crypto.subtle.verify('Ed25519', key, fields.signature, join(label('browser'), nonce)))) return this.acknowledge(false, 'bad_signature');
        this.proven = { id: hex(fields.key.slice(0, 8)), label: fields.label ?? '' };
        this.trusted = this.browsers.some(({ id }) => id === this.proven.id);
        return this.acknowledge(this.trusted, this.trusted ? 'trusted' : 'untrusted');
      }
      if (!this.trusted) return this.acknowledge(false, 'untrusted');
      if (command === 'browsers') {
        const entry = this.browsers[fields.index];
        return this.reply({ command: 'browser', index: fields.index, count: this.browsers.length, ...(entry ?? {}) });
      }
      const id = hex(fields.id);
      if (!this.browsers.some(entry => entry.id === id)) return this.acknowledge(false, 'unknown_browser');
      this.browsers = this.browsers.filter(entry => entry.id !== id);
      if (id === this.proven.id) {
        this.proven = null;
        this.trusted = false;
      }
      return this.acknowledge(true, 'forgotten');
    }

    async press() {
      const task = this.pending;
      this.pending = null;
      if (['set', 'dfu'].includes(task) && this.proven && !this.trusted) {
        this.browsers = [...this.browsers, this.proven];
        this.trusted = true;
      }
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

    async attest({ server_nonce }) {
      const { device_id, unit_public_key, unit_seed } = this.vectors;
      const key = await crypto.subtle.importKey('pkcs8', bytes(PKCS8_ED25519, unit_seed), { name: 'Ed25519' }, false, ['sign']);
      const release = !this.downgrade;
      const message = join(label('attestation'), bytes(device_id, unit_public_key), server_nonce, Uint8Array.of(release ? 1 : 0), encoder.encode(this.running));
      const signature = hex(await crypto.subtle.sign('Ed25519', key, message));
      this.reply({ command: 'attestation', device_id, version: this.running, release, signature });
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
        // INFO: fc 13nov26 an install that does not land comes back on the version it had, as failure says
        if (this.failure) return Object.assign(this, this.failure);
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
