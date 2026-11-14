import { hasWebBluetooth, permittedDevices, requestDevice } from '#skyblip/ble';
import { ActivationWitness, DeviceWitness } from '#skyblip/device_journal';
import { journalOrNull } from '#skyblip/journal';
import { LINKED_PHASES, RETURN_EVERY_MS, Updater, productOf } from '#skyblip/update';

export { hasWebBluetooth };

const STORAGE_KEY = 'skyblip.devices.v1';
const PRODUCT_NAME = /^skyBlip (Go\+?|Bravo) ([0-9A-F]+)$/;
const ACTIVATION_NAME = /^activate ([0-9A-F]+) skyBlip$/;
const SLUG = /^(go|bravo|activate)-([0-9a-f]+)$/;

// INFO: fc 11nov26 a Go and a Go+ of one address are one unit, since a T-Echo is one or the other for life
export function slugOf(name) {
  const product = name?.match(PRODUCT_NAME);
  if (product) return `${product[1].startsWith('Go') ? 'go' : 'bravo'}-${product[2].toLowerCase()}`;
  const activation = name?.match(ACTIVATION_NAME);
  if (activation) return `activate-${activation[1].toLowerCase()}`;
  return null;
}

export function namesOf(slug) {
  const [, kind, tag] = slug.match(SLUG) ?? [];
  const code = tag?.toUpperCase();
  if (kind === 'go') return [`skyBlip Go ${code}`, `skyBlip Go+ ${code}`];
  if (kind === 'bravo') return [`skyBlip Bravo ${code}`];
  if (kind === 'activate') return [`activate ${code} skyBlip`];
  return [];
}

export function nameOf(slug) {
  return namesOf(slug)[0] ?? slug;
}

function newestOf({ upgrade, file, running }, before) {
  if (upgrade === true) return file.version;
  if (upgrade === false) return running;
  return before.newest ?? null;
}

function recordOf(slug, state, before = {}) {
  const { device, running, image, settings, address } = state;
  return {
    slug,
    name: slugOf(device) === slug ? device : before.name ?? nameOf(slug),
    product: image ? productOf(image) : before.product ?? null,
    address: address ?? before.address ?? null,
    running: running ?? before.running ?? null,
    newest: newestOf(state, before),
    track: image?.key ? (image.downgrade ? 'development' : 'release') : before.track ?? null,
    settings: settings ?? before.settings ?? null,
    seen: before.seen ?? null,
  };
}

export class Unit {
  attempted = null;
  witness = new DeviceWitness();
  #fleet;
  #updater = null;
  #activator = null;

  constructor(fleet, slug, record = null) {
    this.#fleet = fleet;
    this.slug = slug;
    this.record = record ?? { slug, name: nameOf(slug) };
  }

  get updater() {
    this.#updater ??= this.#fleet.updaterFor(this, null);
    return this.#updater;
  }

  get state() {
    return this.#updater?.state ?? null;
  }

  get name() {
    const live = this.state?.device;
    return slugOf(live) === this.slug ? live : this.record.name;
  }

  get product() {
    return this.state?.image ? productOf(this.state.image) : this.record.product;
  }

  get remembered() {
    return !this.slug.startsWith('activate-') && slugOf(this.name) !== null;
  }

  get linked() {
    return LINKED_PHASES.has(this.state?.phase);
  }

  get busy() {
    return ['connecting', 'rebooting'].includes(this.state?.phase);
  }

  get handle() {
    return this.#updater?.bluetoothDevice ?? null;
  }

  get needsActivation() {
    const state = this.state;
    return Boolean(state?.settings) && state.address === null && productOf(state.image) === 'skyblip_go';
  }

  activator(build) {
    if (!this.#activator) {
      const witness = new ActivationWitness();
      this.#activator = build({ device: this.handle, onChange: state => this.#fleet.activationChanged(this, witness, state) });
    }
    return this.#activator;
  }

  attach(device) {
    if (this.handle === device) return;
    this.#updater = this.#fleet.updaterFor(this, device);
  }

  connect() {
    return this.updater.connect();
  }

  disconnect() {
    this.#updater?.disconnect();
  }
}

export class Fleet {
  #storage;
  #units = new Map();
  #listeners = new Set();
  #returnEveryMs;
  #restored = null;
  #seenThisSession = new WeakSet();
  #journal;

  constructor({ storage = null, returnEveryMs = RETURN_EVERY_MS, journal = journalOrNull() } = {}) {
    this.#storage = storage;
    this.#returnEveryMs = returnEveryMs;
    this.#journal = journal;
    for (const record of this.#recall()) this.#units.set(record.slug, new Unit(this, record.slug, record));
  }

  get units() {
    return [...this.#units.values()];
  }

  get remembered() {
    return this.units.filter(unit => unit.remembered);
  }

  unit(slug) {
    return this.#units.get(slug) ?? null;
  }

  listen(listener) {
    this.#listeners.add(listener);
    return () => this.#listeners.delete(listener);
  }

  changed() {
    for (const listener of this.#listeners) listener(this);
  }

  activationChanged(unit, witness, state) {
    this.#record(unit, witness.witness(state));
    this.changed();
  }

  // INFO: fc 11nov26 the browser's chooser is the only way to a device it never granted, so a new one always passes through it
  async connectNew({ activation = false } = {}) {
    const device = await this.#choose(activation ? { namePrefix: 'activate' } : {}, activation ? 'activation' : 'new');
    if (!device) return null;
    const unit = this.#unitFor(device);
    if (!unit.linked) await unit.connect();
    return unit;
  }

  async connect(slug) {
    const unit = this.unit(slug) ?? this.#add(new Unit(this, slug));
    if (!unit.handle) {
      const device = await this.#choose({ names: namesOf(slug) }, 'named', slug);
      if (!device) return unit;
      unit.attach(device);
    }
    await unit.connect();
    return unit;
  }

  // INFO: fc 11nov26 getDevices() hands back permitted devices only behind a Chrome flag today, so most browsers connect nothing here
  restore() {
    this.#restored ??= this.#restore();
    return this.#restored;
  }

  async #restore() {
    const devices = await permittedDevices();
    const known = devices.filter(device => this.unit(slugOf(device.name))?.handle === null);
    for (const device of known) this.unit(slugOf(device.name)).attach(device);
    await Promise.allSettled(known.map(device => this.unit(slugOf(device.name)).connect()));
  }

  forget(slug) {
    const unit = this.unit(slug);
    if (!unit) return;
    unit.disconnect();
    unit.handle?.forget?.().catch(error => console.warn('The browser kept its permission for this device:', error.message));
    this.#journal?.record('device.forgotten', {}, { device: slug });
    this.#units.delete(slug);
    this.#remember();
    this.changed();
  }

  updaterFor(unit, device) {
    return new Updater({ device, returnEveryMs: this.#returnEveryMs, onChange: state => this.#heard(unit, state) });
  }

  // INFO: fc 14nov26 Chrome closes the chooser as NotFoundError both when the pilot cancels and when Bluetooth is off, and only the message tells them apart
  async #choose(options, filter, slug = null) {
    try {
      return await requestDevice(options);
    } catch (error) {
      const closed = error.name === 'NotFoundError';
      this.#journal?.record(closed ? 'bluetooth.chooser_closed' : 'bluetooth.chooser_failed', { filter, error: error.name, detail: error.message }, { device: slug });
      if (closed) return null;
      throw error;
    }
  }

  #unitFor(device) {
    const slug = slugOf(device.name) ?? `device-${device.id ?? this.#units.size}`;
    const unit = this.unit(slug) ?? this.#add(new Unit(this, slug));
    unit.attach(device);
    return unit;
  }

  #add(unit) {
    this.#units.set(unit.slug, unit);
    this.changed();
    return unit;
  }

  #heard(unit, state) {
    if (unit.slug.startsWith('activate-') && state.address) this.#rename(unit, `go-${state.address.toLowerCase()}`);
    this.#record(unit, unit.witness.witness(state, { attempted: unit.attempted }));
    if (unit.remembered && LINKED_PHASES.has(state.phase)) {
      const record = recordOf(unit.slug, state, unit.record);
      const changed = JSON.stringify(record) !== JSON.stringify(unit.record);
      unit.record = record;
      if (changed || !this.#seenThisSession.has(unit)) {
        this.#seenThisSession.add(unit);
        unit.record.seen = new Date().toISOString();
        this.#remember();
      }
    }
    this.changed();
  }

  #record(unit, entries) {
    for (const [name, properties] of entries) this.#journal?.record(name, properties, { device: unit.slug });
  }

  #rename(unit, slug) {
    this.#units.delete(unit.slug);
    unit.slug = slug;
    unit.record = { ...(this.unit(slug)?.record ?? {}), slug, name: nameOf(slug) };
    this.#units.set(slug, unit);
  }

  // INFO: fc 11nov26 a browser that blocks site data still connects, it only forgets devices on reload
  #recall() {
    try {
      const records = JSON.parse(this.#storage?.getItem(STORAGE_KEY) ?? '[]');
      return Array.isArray(records) ? records.filter(record => SLUG.test(record?.slug)) : [];
    } catch (error) {
      console.warn('The devices this browser remembered could not be read:', error.message);
      return [];
    }
  }

  #remember() {
    try {
      this.#storage?.setItem(STORAGE_KEY, JSON.stringify(this.remembered.map(unit => unit.record)));
    } catch (error) {
      console.warn('This browser could not remember its devices:', error.message);
    }
  }
}

let shared = null;

function localStorageOrNull() {
  try {
    return globalThis.localStorage ?? null;
  } catch (error) {
    console.warn('This browser blocks site data, so it will not remember devices:', error.message);
    return null;
  }
}

// INFO: fc 11nov26 Turbo swaps pages without reloading this module, so every link outlives the page that opened it
export function joinFleet(listener) {
  shared ??= new Fleet({ storage: localStorageOrNull() });
  const leave = shared.listen(listener);
  return { fleet: shared, leave };
}
