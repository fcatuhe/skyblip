(() => {
  const CONFIG_SERVICE = '69c21301-0187-4204-a91d-02eb8858b440';
  const CONFIG = '69c21302-0187-4204-a91d-02eb8858b440';
  const PKCS8_ED25519 = '302e020100300506032b657004220420';
  const encoder = new TextEncoder();
  const decoder = new TextDecoder();
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

  const unit = {
    vectors: null,
    nonce: null,
    serverNonce: null,
    address: null,
    listeners: [],
    commands: [],

    async press() {
      const { device_id, unit_public_key, unit_seed, server_key_id, release, version } = this.vectors;
      const key = await crypto.subtle.importKey('pkcs8', bytes(PKCS8_ED25519, unit_seed), { name: 'Ed25519' }, false, ['sign']);
      const message = join(
        label('registration'), bytes(device_id, unit_public_key, this.nonce, this.serverNonce, server_key_id), Uint8Array.of(release ? 1 : 0),
        encoder.encode(version),
      );
      const signature = hex(await crypto.subtle.sign('Ed25519', key, message));
      this.reply({ cmd: 'registration', device_id, public_key: unit_public_key, version, server_key_id, release, signature });
    },

    async verifies(signature, message) {
      const key = await crypto.subtle.importKey('raw', bytes(this.vectors.server_public_key), { name: 'Ed25519' }, false, ['verify']);
      return crypto.subtle.verify('Ed25519', key, bytes(signature), message);
    },

    reply(json) {
      const value = new DataView(encoder.encode(JSON.stringify(json)).buffer);
      setTimeout(() => this.listeners.forEach(listener => listener({ target: { value } })));
    },

    async write(raw) {
      const { cmd, ...fields } = JSON.parse(decoder.decode(raw));
      this.commands.push(cmd);
      if (cmd === 'get') return this.reply({ cmd: 'config', version: 1, ...(this.address === null ? {} : { address: this.address }), address_mapping_table: 58 });
      if (cmd === 'challenge') {
        this.nonce = hex(crypto.getRandomValues(new Uint8Array(16)));
        return this.reply({ cmd: 'challenge', nonce: this.nonce, server_key_id: this.vectors.server_key_id });
      }
      if (cmd === 'register') {
        if (!(await this.verifies(fields.signature, join(label('register'), bytes(this.nonce, fields.server_nonce))))) {
          return this.reply({ ack: false, reason: 'bad_signature' });
        }
        this.serverNonce = fields.server_nonce;
        return this.reply({ ack: false, pending: true, reason: 'confirm_register' });
      }
      if (cmd === 'unlock') {
        const serial = new Uint8Array([fields.serial >> 8, fields.serial & 0xff]);
        const message = join(label('unlock'), bytes(this.vectors.device_id, this.vectors.unit_public_key), serial, bytes(this.nonce));
        this.nonce = null;
        if (!(await this.verifies(fields.signature, message))) return this.reply({ ack: false, reason: 'bad_signature' });
        this.address = fields.serial;
        return this.reply({ ack: true, reason: 'unlock' });
      }
      return this.reply({ ack: false, reason: 'unknown_cmd' });
    },
  };

  const characteristic = {
    startNotifications: async () => {},
    addEventListener: (type, listener) => unit.listeners.push(listener),
    writeValueWithResponse: bytes => unit.write(bytes),
  };

  const server = {
    getPrimaryService: async uuid => {
      if (uuid !== CONFIG_SERVICE) throw new Error(`no service ${uuid}`);
      return { getCharacteristic: async id => (id === CONFIG ? characteristic : Promise.reject(new Error(id))) };
    },
  };

  window.fakeUnit = unit;
  Object.defineProperty(navigator, 'bluetooth', {
    configurable: true,
    value: {
      requestDevice: async () => ({
        name: 'activate 3F2A skyBlip',
        addEventListener: () => {},
        gatt: { connected: true, connect: async () => server, disconnect: () => {} },
      }),
    },
  });
})();
