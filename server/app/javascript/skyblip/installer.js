import { IDENTIFY_WITHIN_MS, MeshtasticLink } from '#skyblip/meshtastic';
import { LAYOUTS, Uf2Error, boardOf, encodeUf2, imageOf, parseUf2, trustRecord, withTrustRecord } from '#skyblip/uf2';

// INFO: fc 17nov26 Adafruit's USB vendor id, which LilyGO's T-Echo builds use, and Seeed's, which the X1's do
export const USB_VENDORS = [0x239a, 0x2886];
export const BAUD_RATE = 115200;
export const DRIVE_FILE = 'SKYBLIP.UF2';
const INFO_FILE = 'INFO_UF2.TXT';
export const DRIVE_NAMES = { go: 'TECHOBOOT', bravo: 'X1' };
// INFO: fc 18nov26 read from their build files, unverified on a unit: the T-Echo bootloader, Meshtastic's T-Echo build, and SoftRF's
const FIRMWARE_OF_PORT = { 0x239a: { 0x0029: 'bootloader', 0x002a: 'bootloader', 0x4405: 'meshtastic', 0x8029: 'softrf' } };
const PRODUCT_OF_VENDOR = { 0x239a: 'go', 0x2886: 'bravo' };

export function portOf(info = {}) {
  const vendor = info.usbVendorId ?? null;
  const id = info.usbProductId ?? null;
  return { vendor, id, firmware: FIRMWARE_OF_PORT[vendor]?.[id] ?? 'unknown', product: PRODUCT_OF_VENDOR[vendor] ?? null };
}

export function hasUsbInstall(scope = globalThis) {
  return Boolean(scope.navigator?.serial && typeof scope.showDirectoryPicker === 'function');
}

export function browserSupport(scope = globalThis) {
  if (hasUsbInstall(scope)) return 'usb';
  const mobile = scope.navigator?.userAgentData?.mobile ?? /Android|iPhone|iPad/.test(scope.navigator?.userAgent ?? '');
  return mobile ? 'phone' : 'other';
}

async function textOf(directory, name) {
  try {
    return await (await (await directory.getFileHandle(name)).getFile()).text();
  } catch (error) {
    if (error.name === 'NotFoundError' || error.name === 'TypeMismatchError') return null;
    throw error;
  }
}

export class Installer {
  #onChange;
  #serial;
  #pickDrive;
  #download;
  #browserKey;
  #identifyWithinMs;
  #journal;
  #since = performance.now();
  #state = { phase: 'idle', product: null, tag: null, board: null, drive: null, meshtastic: null, version: null, trusted: false, notice: null };

  constructor({ onChange, serial, pickDrive, download, browserKey, identifyWithinMs = IDENTIFY_WITHIN_MS, journal = null }) {
    this.#onChange = onChange;
    this.#journal = journal;
    this.#identifyWithinMs = identifyWithinMs;
    this.#serial = serial;
    this.#pickDrive = pickDrive;
    this.#download = download;
    this.#browserKey = browserKey;
  }

  get state() {
    return this.#state;
  }

  // INFO: fc 18nov26 the port's USB ids say which firmware it runs, so a bootloader or a firmware that cannot restart skips the four-second wait
  async restart() {
    this.#set({ phase: 'choosing_port', notice: null });
    let port;
    try {
      port = await this.#serial.requestPort({ filters: USB_VENDORS.map(usbVendorId => ({ usbVendorId })) });
    } catch (error) {
      if (error.name === 'NotFoundError') return this.#set({ phase: 'idle', notice: { key: 'port_closed' } });
      return this.#set({ phase: 'idle', notice: { key: 'port_failed', detail: error.message } });
    }
    const usb = portOf(port.getInfo?.());
    this.#record('installer.port_chosen', { usb_vendor: usb.vendor, usb_product: usb.id, firmware: usb.firmware });
    if (usb.firmware === 'bootloader') return this.#set({ phase: 'drive', product: usb.product, drive: 'ready' });
    if (usb.firmware === 'softrf') return this.#set({ phase: 'drive', product: usb.product, drive: 'gesture' });
    this.#set({ phase: 'reading' });
    try {
      await port.open({ baudRate: BAUD_RATE });
    } catch (error) {
      return this.#set({ phase: 'idle', notice: { key: 'port_busy', detail: error.message } });
    }
    const link = new MeshtasticLink(port);
    let identity = null;
    try {
      identity = await link.identify({ withinMs: this.#identifyWithinMs });
      if (identity?.product) await link.restartIntoDrive(identity.nodeNum);
    } catch (error) {
      console.warn('The node did not answer as Meshtastic:', error.message);
    }
    await link.close();
    await port.close().catch(error => console.warn('The serial port did not close, it is probably gone already:', error.message));
    if (identity) this.#record('installer.identified', { product: identity.product, hw_model: identity.hwModel, version: identity.firmwareVersion });
    if (!identity) return this.#set({ phase: 'drive', product: usb.product, drive: 'gesture' });
    if (!identity.product) return this.#set({ phase: 'idle', meshtastic: identity, notice: { key: 'other_board' } });
    this.#set({ phase: 'drive', product: identity.product, tag: identity.tag, meshtastic: identity, drive: 'restarted' });
  }

  async install() {
    this.#set({ phase: 'choosing_drive', notice: null });
    let drive;
    try {
      drive = await this.#pickDrive();
    } catch (error) {
      const key = error.name === 'AbortError' ? null : error.name === 'NotAllowedError' || error.name === 'SecurityError' ? 'drive_denied' : 'drive_failed';
      return this.#set({ phase: 'drive', notice: key && { key, detail: error.message } });
    }
    const board = boardOf(await textOf(drive, INFO_FILE));
    if (!board.product) return this.#set({ phase: 'drive', notice: { key: board.id ? 'other_board' : 'not_drive', detail: board.id } });
    const product = board.product;
    this.#record('installer.drive_chosen', { board: board.id, product });
    this.#set({ phase: 'downloading', product, board: board.id, tag: product === this.#state.product ? this.#state.tag : null });
    let image;
    try {
      image = await this.#image(product);
    } catch (error) {
      return this.#set({ phase: 'drive', notice: { key: error instanceof Uf2Error ? `image_${error.key}` : 'download_failed', detail: error.message } });
    }
    this.#set({ phase: 'writing', version: image.version, trusted: image.trusted });
    await this.#write(drive, image.bytes);
  }

  async #image(product) {
    const { bytes, version } = await this.#download(product);
    const uf2 = parseUf2(bytes);
    const layout = LAYOUTS[product];
    if (imageOf(uf2.blocks, layout)?.product !== product) throw new Uf2Error('wrong_product');
    const key = await this.#browserKey().catch(error => {
      console.warn('This browser has no key, so the unit will ask for its presses:', error.message);
      return null;
    });
    if (!key) return { bytes, version, trusted: false };
    return { bytes: encodeUf2(withTrustRecord(uf2, layout, trustRecord(key))), version, trusted: true };
  }

  // INFO: fc 17nov26 the bootloader restarts the unit once the last block lands, which can fail the close that follows
  async #write(drive, bytes) {
    let writable = null;
    const started = performance.now();
    try {
      const file = await drive.getFileHandle(DRIVE_FILE, { create: true });
      writable = await file.createWritable();
      await writable.write(bytes);
    } catch (error) {
      writable?.abort().catch(aborted => console.warn('The half-written file could not be dropped:', aborted.message));
      return this.#set({ phase: 'drive', notice: { key: 'write_failed', detail: error.message } });
    }
    try {
      await writable.close();
    } catch (error) {
      console.warn('The drive went away as the image landed, as a restarting bootloader does:', error.message);
    }
    const { product, version, trusted } = this.#state;
    this.#record('installer.wrote', { product, version, trusted, write_ms: Math.round(performance.now() - started) });
    this.#set({ phase: 'installed', notice: null });
  }

  #record(name, properties) {
    this.#journal?.record(name, properties);
  }

  #set(patch) {
    const before = this.#state;
    this.#state = { ...before, ...patch };
    if (this.#state.phase !== before.phase) {
      const now = performance.now();
      this.#record('installer.stepped', { phase: this.#state.phase, previous: before.phase, lasted_ms: Math.round(now - this.#since) });
      this.#since = now;
    }
    const notice = this.#state.notice;
    if (notice && notice !== before.notice) this.#record('installer.noticed', { key: notice.key, detail: notice.detail ?? null, phase: this.#state.phase });
    this.#onChange(this.#state);
  }
}
