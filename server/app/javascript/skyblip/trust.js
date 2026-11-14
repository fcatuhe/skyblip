import { hex } from '#skyblip/link';
import { Replies } from '#skyblip/replies';

const DOMAIN = new TextEncoder().encode('skyblip:browser:v1');
const LABEL_BYTES = 24;
const ID_BYTES = 8;
const DATABASE = 'skyblip';
const STORE = 'keys';
const ENTRY = 'browser';
// INFO: fc 13nov26 an answer lost on the link must not hold the page at connecting for good
const ANSWER_WITHIN_MS = 5_000;

export const idOf = publicKey => hex(publicKey.subarray(0, ID_BYTES));

export function signedMessage(nonce) {
  const message = new Uint8Array(DOMAIN.length + nonce.length);
  message.set(DOMAIN);
  message.set(nonce, DOMAIN.length);
  return message;
}

export function labelOf(agent = globalThis.navigator) {
  const data = agent?.userAgentData;
  const brand = data?.brands?.map(({ brand }) => brand).find(name => !/not.a.brand|chromium/i.test(name));
  const name = brand?.replace(/^(Google|Microsoft) /, '') ?? 'Browser';
  return clip(data?.platform ? `${name} on ${data.platform}` : name);
}

function clip(text) {
  const encoder = new TextEncoder();
  let clipped = text;
  while (encoder.encode(clipped).length > LABEL_BYTES) clipped = [...clipped].slice(0, -1).join('');
  return clipped;
}

export async function browserKeyFrom(keys, label = labelOf()) {
  const publicKey = new Uint8Array(await crypto.subtle.exportKey('raw', keys.publicKey));
  return {
    publicKey,
    label,
    sign: async bytes => new Uint8Array(await crypto.subtle.sign({ name: 'Ed25519' }, keys.privateKey, bytes)),
  };
}

// INFO: fc 13nov26 a browser with no IndexedDB or no Ed25519 has no key, and asks for presses as before
export async function storedBrowserKey() {
  if (!globalThis.indexedDB || !globalThis.crypto?.subtle) return null;
  const database = await opened();
  const kept = await request(database.transaction(STORE).objectStore(STORE).get(ENTRY));
  const keys = kept ?? (await generated(database));
  return keys ? browserKeyFrom(keys) : null;
}

// INFO: fc 13nov26 two tabs may each make a key on a first visit, and the one stored first wins
async function generated(database) {
  let keys;
  try {
    keys = await crypto.subtle.generateKey({ name: 'Ed25519' }, false, ['sign', 'verify']);
  } catch (error) {
    if (error.name === 'NotSupportedError') return null;
    throw error;
  }
  try {
    await request(database.transaction(STORE, 'readwrite').objectStore(STORE).add(keys, ENTRY));
    return keys;
  } catch (error) {
    if (error.name !== 'ConstraintError') throw error;
    return request(database.transaction(STORE).objectStore(STORE).get(ENTRY));
  }
}

function opened() {
  const opening = indexedDB.open(DATABASE, 1);
  opening.onupgradeneeded = () => opening.result.createObjectStore(STORE);
  return request(opening);
}

function request(pending) {
  return new Promise((resolve, reject) => {
    pending.onsuccess = () => resolve(pending.result);
    pending.onerror = () => reject(pending.error);
  });
}

export class BrowserTrust {
  #replies;
  #loadKey;
  #onChange;
  #key = null;
  #trust = null;
  #browsers = [];
  #listing = null;

  constructor({ send, loadKey, onChange }) {
    this.#replies = new Replies({ send, withinMs: ANSWER_WITHIN_MS });
    this.#loadKey = loadKey;
    this.#onChange = onChange;
  }

  get trusted() {
    return this.#trust === 'trusted';
  }

  // INFO: fc 13nov26 claimed and small_mtu also reach the updater, which tells the pilot about the link
  receive(reply) {
    if (!this.#replies.take(reply)) return reply.command === 'acknowledgement' && TRUST_REASONS.has(reply.reason);
    return !LINK_REASONS.has(reply.reason);
  }

  close() {
    this.#replies.close();
    this.#change(null, null);
  }

  // INFO: fc 13nov26 a key the browser cannot keep is no reason to refuse the unit: the presses still work
  async prove() {
    this.#replies.open();
    try {
      this.#key ??= await this.#loadKey();
    } catch (error) {
      console.error('no browser key, so every change asks for two presses', error);
    }
    if (!this.#key) return;
    const challenge = await this.#replies.ask({ command: 'browser_challenge' }, reply => reply.command === 'browser_challenge' || refusal(reply, PROOF_REFUSALS));
    if (challenge?.command !== 'browser_challenge') return;
    const signature = await this.#key.sign(signedMessage(challenge.nonce));
    const verdict = await this.#replies.ask(
      { command: 'trust_browser', key: this.#key.publicKey, signature, label: this.#key.label },
      reply => refusal(reply, PROOF_VERDICTS),
    );
    this.#change({ trusted: 'trusted', untrusted: 'proven' }[verdict?.reason] ?? null, null);
  }

  // INFO: fc 13nov26 the unit stores a proven key on the set or dfu two presses confirm on this link
  confirmed() {
    if (this.#trust === 'proven') this.#change('trusted', this.#browsers);
  }

  list() {
    if (!this.trusted) return;
    this.#listing ??= this.#read().finally(() => (this.#listing = null));
    return this.#listing;
  }

  // INFO: fc 13nov26 a list that did not arrive is shown empty, so the page does not ask again on every render
  async #read() {
    const browsers = [];
    for (let index = 0; ; index++) {
      const entry = await this.#replies.ask({ command: 'browsers', index }, reply => reply.command === 'browser' || refusal(reply, LIST_REFUSALS));
      if (entry?.reason === 'untrusted') return this.#change(null, null);
      if (entry?.command !== 'browser') return this.#change(this.#trust, []);
      if (index >= entry.count) return this.#change(this.#trust, browsers);
      const id = hex(entry.id);
      browsers.push({ id, label: entry.label, self: id === idOf(this.#key.publicKey) });
    }
  }

  async forget(ids) {
    if (!this.trusted) return null;
    for (const id of ids) {
      const verdict = await this.#replies.ask({ command: 'forget_browser', id }, reply => refusal(reply, FORGET_VERDICTS));
      if (verdict?.reason !== 'forgotten') return verdict?.reason ?? 'timeout';
      if (id === idOf(this.#key.publicKey)) return this.#change(null, null);
    }
    return this.list();
  }

  #change(trust, browsers) {
    this.#trust = trust;
    this.#browsers = browsers;
    this.#onChange({ trust, browsers });
  }
}

const LINK_REASONS = new Set(['claimed', 'small_mtu']);
// INFO: fc 13nov26 only this exchange answers these, so a verdict that arrives after its timeout is still ours
const TRUST_REASONS = new Set(['trusted', 'untrusted', 'forgotten', 'unknown_browser']);
const PROOF_REFUSALS = new Set(['keyless', 'unknown_command', 'small_mtu', 'claimed']);
const PROOF_VERDICTS = new Set(['trusted', 'untrusted', 'no_session', 'bad_signature', 'invalid', 'unknown_command', 'small_mtu', 'claimed']);
const LIST_REFUSALS = new Set(['untrusted', 'unknown_command', 'small_mtu', 'claimed']);
const FORGET_VERDICTS = new Set(['forgotten', 'unknown_browser', 'untrusted', 'invalid', 'unknown_command', 'small_mtu', 'claimed']);

function refusal(reply, reasons) {
  return reply.command === 'acknowledgement' && reasons.has(reply.reason);
}
