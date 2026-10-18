import { addressOf, connect, hasWebBluetooth } from '#skyblip/ble';

export { hasWebBluetooth };

const BUSY = new Set(['asking', 'confirming', 'registering', 'unlocking']);

export class ServerRefusal extends Error {
  constructor(key, detail = null) {
    super(detail ? `${key}: ${detail}` : key);
    this.key = key;
    this.detail = detail;
  }
}

export function httpServer({ challenges, registrations, token = null }) {
  async function post(url, body) {
    let response;
    try {
      response = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', Accept: 'application/json', ...(token ? { 'X-CSRF-Token': token } : {}) },
        body: JSON.stringify(body),
      });
    } catch (error) {
      throw new ServerRefusal('unreachable', error.message);
    }
    const reply = await response.json().catch(() => null);
    if (response.ok && reply) return reply;
    if (reply?.error) throw new ServerRefusal(reply.error);
    throw new ServerRefusal('failed', `HTTP ${response.status}`);
  }

  return {
    challenge: body => post(challenges, body),
    register: body => post(registrations, body),
  };
}

export class Activator {
  #onChange;
  #server;
  #device = null;
  #leaving = false;
  #serverNonce = null;
  #state = { phase: 'offline', device: null, address: null, read: false, notice: null };

  constructor({ onChange, server }) {
    this.#onChange = onChange;
    this.#server = server;
  }

  get state() {
    return this.#state;
  }

  async connect() {
    if (!hasWebBluetooth()) return this.#set({ notice: { key: 'no_bluetooth' } });
    this.#leaving = false;
    this.#set({ phase: 'connecting', notice: null });
    let device;
    try {
      device = await connect({ onReply: reply => this.#receive(reply), onClose: () => this.#closed() });
    } catch (error) {
      return this.#set({ phase: 'offline', notice: error.name === 'NotFoundError' ? null : { key: 'failed', detail: error.message } });
    }
    if (!device.hasConfig) {
      this.#leaving = true;
      device.disconnect();
      return this.#set({ phase: 'offline', notice: { key: 'not_skyblip' } });
    }
    this.#device = device;
    this.#set({ phase: 'ready', device: device.name });
    await this.#send({ cmd: 'get' });
  }

  disconnect() {
    this.#leaving = true;
    if (this.#device) this.#device.disconnect();
  }

  activate() {
    if (this.#state.phase !== 'ready') return;
    this.#set({ phase: 'asking', notice: null });
    return this.#send({ cmd: 'hello' });
  }

  async #send(command) {
    try {
      await this.#device.sendConfig(command);
    } catch (error) {
      this.#set({ phase: this.#device ? 'ready' : 'offline', notice: { key: 'failed', detail: error.message } });
    }
  }

  #receive(reply) {
    if (!reply) return;
    if (reply.cmd === 'config') this.#set({ address: addressOf(reply), read: true });
    else if (reply.cmd === 'hello' && this.#state.phase === 'asking') return this.#challenge(reply);
    else if (reply.cmd === 'registration' && this.#state.phase === 'confirming') return this.#register(reply);
    else if ('ack' in reply) this.#acked(reply);
  }

  #acked(reply) {
    const { phase } = this.#state;
    if (reply.reason === 'claimed') return this.#set({ phase: 'ready', notice: { key: 'claimed' } });
    if (!BUSY.has(phase)) return;
    if (reply.pending && reply.reason === 'confirm_register') return this.#set({ phase: 'confirming' });
    if (reply.ack && reply.reason === 'unlock' && phase === 'unlocking') return this.#unlocked();
    this.#set({ phase: 'ready', notice: { key: reply.reason || 'refused' } });
  }

  async #challenge({ nonce, server_key }) {
    const answer = await this.#ask(() => this.#server.challenge({ unit_nonce: nonce, server_key }));
    if (!answer) return;
    this.#serverNonce = answer.server_nonce;
    return this.#send({ cmd: 'register', server_nonce: answer.server_nonce, signature: answer.signature });
  }

  async #register({ cmd, ...registration }) {
    this.#set({ phase: 'registering' });
    const answer = await this.#ask(() => this.#server.register({ ...registration, server_nonce: this.#serverNonce }));
    if (!answer) return;
    this.#set({ phase: 'unlocking' });
    return this.#send({ cmd: 'unlock', serial: answer.serial, signature: answer.signature });
  }

  async #ask(request) {
    try {
      return await request();
    } catch (error) {
      if (!(error instanceof ServerRefusal)) throw error;
      if (this.#device) this.#set({ phase: 'ready', notice: { key: `server_${error.key}`, detail: error.detail } });
      return null;
    }
  }

  #unlocked() {
    this.#set({ phase: 'ready', notice: { key: 'activated' } });
    return this.#send({ cmd: 'get' });
  }

  #closed() {
    const notice = BUSY.has(this.#state.phase) && !this.#leaving ? { key: 'link_lost' } : this.#state.notice;
    this.#device = null;
    this.#serverNonce = null;
    this.#set({ phase: 'offline', device: null, address: null, read: false, notice });
  }

  #set(patch) {
    this.#state = { ...this.#state, ...patch };
    this.#onChange(this.#state);
  }
}
