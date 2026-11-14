const OUTBOX_KEY = 'skyblip.journal.v1';
const BROWSER_KEY = 'skyblip.browser.v1';
const CROCKFORD = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
const BROWSER = /^[0-9A-HJKMNP-TV-Z]{8}$/;
// INFO: fc 14nov26 a phone offline for a weekend of flying keeps its last 500 entries, and the oldest go first
const KEPT = 500;
// INFO: fc 14nov26 a keepalive fetch carries 64 KB at most, and 50 entries stay well under it
const BATCH = 50;
const FLUSH_AFTER_MS = 2_000;
const RETRY_AFTER_MS = 30_000;
// INFO: fc 14nov26 the support code is a tracker in the CNIL's sense, so it lives 13 months from its minting and is never renewed
const BROWSER_LIFETIME_MONTHS = 13;

export function browserCodeOf(bytes) {
  return [...bytes.slice(0, 8)].map(byte => CROCKFORD[byte % 32]).join('');
}

export function supportCodeOf(browser) {
  return browser ? `${browser.slice(0, 4)}-${browser.slice(4)}` : null;
}

const BROWSERS = [
  ['Edge', /Edg(?:A|iOS)?\/(\d+)/], ['Opera', /OPR\/(\d+)/], ['Samsung Internet', /SamsungBrowser\/(\d+)/],
  ['Firefox', /(?:Firefox|FxiOS)\/(\d+)/], ['Chrome', /(?:Chrome|CriOS)\/(\d+)/], ['Safari', /Version\/(\d+).*Safari/],
];
const SYSTEMS = [['Android', /Android/], ['iOS', /iPhone|iPad|iPod/], ['ChromeOS', /CrOS/], ['macOS', /Mac OS X/], ['Windows', /Windows/], ['Linux', /Linux/]];

// INFO: fc 14nov26 a browser is named by family, major version and system alone, never by its whole user agent
export function browserFactsOf(agent) {
  const ua = agent?.userAgent ?? '';
  const [name, major] = BROWSERS.map(([family, pattern]) => [family, ua.match(pattern)?.[1]]).find(([, version]) => version) ?? [null, null];
  return {
    browser_name: name,
    browser_major: major === null ? null : Number(major),
    os_name: agent?.userAgentData?.platform || SYSTEMS.find(([, pattern]) => pattern.test(ua))?.[0] || null,
    mobile: agent?.userAgentData?.mobile ?? /Mobi|Android|iPhone|iPad/.test(ua),
    language: agent?.language?.slice(0, 35) ?? null,
  };
}

export class Journal {
  #storage;
  #send;
  #now;
  #uuid;
  #random;
  #schedule;
  #path;
  #page;
  #seq = 0;
  #memory = [];
  #browser = null;
  #timer = null;
  #flushing = null;

  constructor({ send, storage = null, now = () => new Date(), uuid = () => crypto.randomUUID(),
    random = size => crypto.getRandomValues(new Uint8Array(size)), schedule = (work, ms) => setTimeout(work, ms),
    path = () => globalThis.location?.pathname ?? null }) {
    this.#send = send;
    this.#storage = storage;
    this.#now = now;
    this.#uuid = uuid;
    this.#random = random;
    this.#schedule = schedule;
    this.#path = path;
    this.#page = uuid();
  }

  get browser() {
    this.#browser ??= this.#recallBrowser() ?? this.#mintBrowser();
    return this.#browser;
  }

  get supportCode() {
    return supportCodeOf(this.browser);
  }

  get pending() {
    return this.#outbox().length;
  }

  record(name, properties = {}, { device = null } = {}) {
    const entry = {
      id: this.#uuid(), name, at: this.#now().toISOString(), seq: ++this.#seq, page: this.#page, path: this.#path(),
      ...(device ? { device } : {}), properties: compact(properties),
    };
    this.#keep([...this.#outbox(), entry]);
    this.#later(FLUSH_AFTER_MS);
    return entry;
  }

  flush() {
    this.#flushing ??= this.#drain().finally(() => (this.#flushing = null));
    return this.#flushing;
  }

  async #drain() {
    for (;;) {
      const batch = this.#outbox().slice(0, BATCH);
      if (batch.length === 0) return true;
      let delivered;
      try {
        delivered = await this.#send({ browser: this.browser, entries: batch });
      } catch (error) {
        console.warn('The journal could not be sent, it waits for the next try:', error.message);
        delivered = false;
      }
      if (!delivered) {
        this.#later(RETRY_AFTER_MS);
        return false;
      }
      const sent = new Set(batch.map(entry => entry.id));
      this.#keep(this.#outbox().filter(entry => !sent.has(entry.id)));
    }
  }

  #later(ms) {
    if (this.#timer !== null) return;
    this.#timer = this.#schedule(() => {
      this.#timer = null;
      this.flush();
    }, ms);
  }

  // INFO: fc 14nov26 two tabs share the outbox, so each change reads it again rather than trusting a copy
  #outbox() {
    if (!this.#storage) return this.#memory;
    try {
      const entries = JSON.parse(this.#storage.getItem(OUTBOX_KEY) ?? '[]');
      return Array.isArray(entries) ? entries : [];
    } catch (error) {
      console.warn('The journal waiting to be sent could not be read:', error.message);
      return [];
    }
  }

  #keep(entries) {
    const kept = entries.slice(-KEPT);
    if (!this.#storage) return (this.#memory = kept);
    try {
      this.#storage.setItem(OUTBOX_KEY, JSON.stringify(kept));
    } catch (error) {
      console.warn('This browser could not keep the journal, it keeps it until the page closes:', error.message);
      this.#storage = null;
      this.#memory = kept;
    }
  }

  #recallBrowser() {
    try {
      const { code, until } = JSON.parse(this.#storage?.getItem(BROWSER_KEY) ?? '{}') ?? {};
      return BROWSER.test(code ?? '') && Date.parse(until) > this.#now().getTime() ? code : null;
    } catch (error) {
      console.warn('This browser could not read its support code:', error.message);
      return null;
    }
  }

  #mintBrowser() {
    const code = browserCodeOf(this.#random(8));
    const until = this.#now();
    until.setMonth(until.getMonth() + BROWSER_LIFETIME_MONTHS);
    try {
      this.#storage?.setItem(BROWSER_KEY, JSON.stringify({ code, until: until.toISOString() }));
    } catch (error) {
      console.warn('This browser could not keep its support code:', error.message);
    }
    return code;
  }
}

function compact(properties) {
  return Object.fromEntries(Object.entries(properties).filter(([, value]) => value !== null && value !== undefined));
}

export function httpTransport({ url, token = null, fetch = globalThis.fetch }) {
  return async ({ browser, entries }) => {
    const response = await fetch(url, {
      method: 'POST',
      keepalive: true,
      credentials: 'same-origin',
      headers: { 'Content-Type': 'application/json', 'X-Briefing-Browser': browser, ...(token ? { 'X-CSRF-Token': token } : {}) },
      body: JSON.stringify({ browser, entries }),
    });
    // INFO: fc 14nov26 a 400 is a journal the server will never take, so it is dropped rather than sent forever
    return response.ok || response.status === 400;
  };
}

let shared = null;

function localStorageOrNull() {
  try {
    return globalThis.localStorage ?? null;
  } catch (error) {
    console.warn('This browser blocks site data, so the journal waits in the page:', error.message);
    return null;
  }
}

export function sharedJournal() {
  shared ??= new Journal({
    storage: localStorageOrNull(),
    send: entries => httpTransport({ url: '/events', token: document.querySelector('meta[name="csrf-token"]')?.content })(entries),
  });
  return shared;
}

export function journalOrNull() {
  return globalThis.document ? sharedJournal() : null;
}
