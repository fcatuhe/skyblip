import { addressOf, connect, hasWebBluetooth } from '#skyblip/ble';
import { hex } from '#skyblip/link';
import { changedSettings, settingsOf, statusOf } from '#skyblip/settings';

export { hasWebBluetooth };
export { readSetting, showSetting } from '#skyblip/settings';

export const OFFERED_SETTINGS = ['aircraft_type', 'callsign'];

const SAVING = new Set(['saving', 'storing']);
const BUSY = new Set(['asking', 'confirming', 'registering', 'unlocking', ...SAVING]);

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
  #state = { phase: 'offline', device: null, address: null, read: false, settings: null, status: null, heldForGround: false, offer: null, notice: null };

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
    await this.#send({ command: 'get' });
  }

  disconnect() {
    this.#leaving = true;
    if (this.#device) this.#device.disconnect();
  }

  activate() {
    if (this.#state.phase !== 'ready') return;
    this.#set({ phase: 'asking', notice: null });
    return this.#send({ command: 'challenge' });
  }

  saveSettings(values) {
    const { phase, offer, settings } = this.#state;
    if (phase !== 'ready' || offer !== 'open') return;
    const changed = changedSettings(settings, values, OFFERED_SETTINGS);
    if (Object.keys(changed).length === 0) return;
    this.#set({ phase: 'saving', notice: null });
    return this.#send({ command: 'set', ...changed });
  }

  skipSettings() {
    if (this.#state.phase === 'ready' && this.#state.offer === 'open') this.#set({ offer: 'skipped', notice: { key: 'skipped' } });
  }

  // INFO: fc 10nov26 a write for a link that closed meanwhile belongs to no unit, and #closed has said why
  async #send(command, device = this.#device) {
    if (!device || device !== this.#device) return false;
    try {
      await device.sendConfig(command);
      return true;
    } catch (error) {
      if (this.#device === device) this.#set({ phase: 'ready', notice: { key: 'failed', detail: error.message } });
      return false;
    }
  }

  #receive(reply) {
    if (!reply) return;
    if (reply.command === 'config') this.#set({ address: addressOf(reply), read: true, settings: settingsOf(reply, OFFERED_SETTINGS) });
    else if (reply.command === 'status') this.#set({ status: statusOf(reply), ...(reply.flight === 'ground' ? { heldForGround: false } : {}) });
    else if (reply.command === 'challenge' && this.#state.phase === 'asking') return this.#challenge(reply);
    else if (reply.command === 'registration' && this.#state.phase === 'confirming') return this.#register(reply);
    else if (reply.command === 'acknowledgement') this.#acked(reply);
  }

  #acked(reply) {
    const { phase } = this.#state;
    if (reply.reason === 'claimed' || reply.reason === 'small_mtu') return this.#set({ phase: 'ready', notice: { key: reply.reason } });
    if (!BUSY.has(phase)) return;
    if (SAVING.has(phase)) return this.#settingsAcked(reply);
    if (reply.pending && reply.reason === 'confirm_register') return this.#set({ phase: 'confirming' });
    if (reply.acknowledge && reply.reason === 'unlock' && phase === 'unlocking') return this.#unlocked();
    this.#set({ phase: 'ready', notice: { key: reply.reason || 'refused' } });
  }

  #settingsAcked(reply) {
    if (reply.pending && reply.reason === 'confirm') return this.#set({ phase: 'storing' });
    if (reply.acknowledge && !reply.reason) return this.#saved();
    this.#set({ phase: 'ready', heldForGround: reply.reason === 'in_flight', notice: { key: reply.reason || 'refused', during: 'set' } });
  }

  #saved() {
    this.#set({ phase: 'ready', offer: 'saved', notice: { key: 'saved' } });
    return this.#send({ command: 'get' });
  }

  async #challenge({ nonce, server_key_id }) {
    const device = this.#device;
    const answer = await this.#ask(device, () => this.#server.challenge({ unit_nonce: hex(nonce), server_key_id: hex(server_key_id) }));
    if (!answer) return;
    this.#serverNonce = answer.server_nonce;
    return this.#send({ command: 'register', server_nonce: answer.server_nonce, signature: answer.signature });
  }

  async #register({ command, ...frame }) {
    const device = this.#device;
    const registration = relayed(frame);
    const unit = `${registration.device_id} ${registration.public_key.slice(0, 8)}`;
    this.#set({ phase: 'registering' });
    const answer = await this.#ask(device, () => this.#server.register({ ...registration, server_nonce: this.#serverNonce }), unit);
    if (!answer) return;
    this.#set({ phase: 'unlocking' });
    return this.#send({ command: 'unlock', serial: answer.serial, signature: answer.signature });
  }

  // INFO: fc 18oct26 an answer for a link that dropped meanwhile belongs to no unit, not to the next one
  async #ask(device, request, unit = null) {
    try {
      const answer = await request();
      return this.#device === device ? answer : null;
    } catch (error) {
      if (!(error instanceof ServerRefusal)) throw error;
      if (this.#device === device) this.#set({ phase: 'ready', notice: { key: `server_${error.key}`, detail: error.detail ?? unit } });
      return null;
    }
  }

  async #unlocked() {
    const device = this.#device;
    this.#set({ phase: 'ready', offer: 'open', notice: { key: 'activated' } });
    if (await this.#send({ command: 'get' }, device)) await this.#send({ command: 'status' }, device);
  }

  #closed() {
    const { phase, offer } = this.#state;
    const notice = (BUSY.has(phase) || offer === 'open') && !this.#leaving ? { key: 'link_lost' } : this.#state.notice;
    this.#device = null;
    this.#serverNonce = null;
    this.#set({ phase: 'offline', device: null, address: null, read: false, settings: null, status: null, heldForGround: false, offer: null, notice });
  }

  #set(patch) {
    this.#state = { ...this.#state, ...patch };
    this.#onChange(this.#state);
  }
}

function relayed(frame) {
  return Object.fromEntries(Object.entries(frame).map(([name, value]) => [name, value instanceof Uint8Array ? hex(value) : value]));
}
