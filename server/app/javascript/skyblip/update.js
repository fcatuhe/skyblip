import { GATT_WRITE_BYTES, addressOf, connect, hasWebBluetooth } from '#skyblip/ble';
import { compareVersions, fullImageNeeded, parseVersion, readImage, sha256, versionText } from '#skyblip/image';
import { hex } from '#skyblip/link';
import { changedSettings, settingsFrame, settingsOf, statusOf } from '#skyblip/settings';
import {
  GROUP, RC, SmpClient, SmpError, SmpTimeout, SmpWriteRejected,
  dataRoom, packetBudget, probeWriteBytes, runningVersion, upload,
} from '#skyblip/smp';

export { hasWebBluetooth };
export { readSetting, showSetting } from '#skyblip/settings';

const IMAGE_REFUSALS = {
  5: 'wrong_product',
  10: 'flash',
  11: 'flash',
  12: 'flash',
  13: 'flash',
  22: 'not_image',
  23: 'not_image',
  27: 'older',
  30: 'too_large',
};

// INFO: fc 26sep26 the ATT payloads of MTU 247 and 185, where Android and iOS commonly settle
const SMALLER_WRITES = [244, 182, GATT_WRITE_BYTES];

const EXPECTED_DROP = new Set(['installing', 'rebooting', 'recovering']);
const BUSY = new Set(['asking', 'confirming', 'uploading']);

class LinkLost extends Error {
  constructor() {
    super('the link dropped');
  }
}

export function noticeOf(error) {
  if (error instanceof SmpError) {
    if (error.group === null && error.rc === RC.accessDenied) return { key: 'window_closed' };
    if (error.group === GROUP.image && IMAGE_REFUSALS[error.rc]) return { key: IMAGE_REFUSALS[error.rc] };
    return { key: 'smp', detail: error.message };
  }
  if (error instanceof SmpTimeout) return { key: 'timeout' };
  if (error instanceof LinkLost) return { key: 'link_lost' };
  return { key: 'failed', detail: error.message };
}

function imageOf(reply) {
  return {
    state: reply.image,
    from: reply.from ?? null,
    to: reply.to ?? null,
    settings: reply.settings ?? null,
    swapPowered: reply.swap_powered !== false,
    imu: reply.imu === 'held' ? hex(reply.imu_digest) : reply.imu,
    downgrade: reply.downgrade === true,
    key: reply.key ? hex(reply.key).toUpperCase() : null,
    product: reply.product ?? null,
  };
}

// INFO: fc 10nov26 a Go leaves product out, as all firmware before the key did, so an unread frame is a Go too
export function productOf(image) {
  return image?.product ?? 'skyblip_go';
}

function identityOf(reply) {
  return { address: addressOf(reply) };
}

function adviceOf({ image, file }) {
  if (!image) return null;
  // INFO: fc 03oct26 before a file is chosen, the blob the device verified stands in for the release's pin
  const pin = file ? file.pin : image.imu;
  return fullImageNeeded(image.imu, pin) ? 'full' : 'slim';
}

function settlingOf({ phase, image }) {
  return phase === 'ready' && (image?.state === 'probation' || image?.imu === 'writing');
}

export class Updater {
  #onChange;
  #device = null;
  #smp = new SmpClient(bytes => this.#device.sendSmp(bytes, this.#writeBytes));
  #packetBytes = null;
  #writeBytes = GATT_WRITE_BYTES;
  #probed = false;
  #running = null;
  #image = null;
  #sha = null;
  #version = null;
  #key = null;
  #product = null;
  #windowOpen = false;
  #leaving = false;
  #state = {
    phase: 'offline',
    task: null,
    device: null,
    image: null,
    running: null,
    status: null,
    settings: null,
    address: null,
    defaults: null,
    refusedOnDevice: false,
    file: null,
    advice: null,
    settling: false,
    progress: null,
    notice: null,
  };

  constructor({ onChange }) {
    this.#onChange = onChange;
  }

  get state() {
    return this.#state;
  }

  get inFlight() {
    return this.#state.status?.flight === 'airborne';
  }

  async connect() {
    if (!hasWebBluetooth()) return this.#set({ notice: { key: 'no_bluetooth' } });
    this.#leaving = false;
    this.#set({ phase: 'connecting', notice: null });
    let device;
    try {
      device = await connect({
        onReply: reply => this.#receive(reply),
        onSmp: bytes => this.#smp.receive(bytes),
        onClose: () => this.#closed(),
      });
    } catch (error) {
      return this.#set({ phase: 'offline', notice: error.name === 'NotFoundError' ? null : noticeOf(error) });
    }
    if (!device.hasConfig || !device.hasSmp) {
      this.#leaving = true;
      device.disconnect();
      return this.#set({ phase: 'offline', notice: { key: 'not_skyblip' } });
    }
    this.#device = device;
    this.#set({ phase: 'ready', device: device.name });
    await this.#learn();
  }

  disconnect() {
    this.#leaving = true;
    if (this.#device) this.#device.disconnect();
  }

  clearImage() {
    this.#image = null;
    this.#set({ file: null, notice: null });
  }

  async choose(bytes, name) {
    const image = readImage(bytes);
    if (!image) {
      this.#image = null;
      return this.#set({ file: null, notice: { key: 'not_image' } });
    }
    this.#image = bytes;
    this.#sha = await sha256(bytes);
    this.#version = image.version;
    const file = { name, version: versionText(image.version), bytes: bytes.length, ...image.imu };
    this.#key = image.key;
    this.#product = image.product;
    this.#set({ file, notice: this.#needsFull(file) ? { key: 'needs_full' } : null });
  }

  async install() {
    if (!this.#image) return this.#set({ notice: { key: 'no_file' } });
    if (this.inFlight) return this.#set({ notice: { key: 'in_flight' } });
    const refusal = this.#refusal();
    if (refusal) return this.#set({ notice: { key: refusal } });
    this.#set({ notice: null });
    if (this.#windowOpen) return this.#upload();
    return this.#ask('dfu', { version: versionText(this.#version) });
  }

  recover() {
    if (this.inFlight) return this.#set({ notice: { key: 'in_flight' } });
    this.#set({ notice: null });
    return this.#ask('recovery');
  }

  refresh() {
    return this.#send({ command: 'update' });
  }

  saveSettings(values) {
    const changed = changedSettings(this.#state.settings, values);
    if (Object.keys(changed).length === 0) return;
    if (this.inFlight) return this.#set({ notice: { key: 'in_flight', during: 'set' } });
    this.#set({ notice: null });
    return this.#ask('set', settingsFrame(changed));
  }

  resetSettings() {
    if (this.#state.defaults) return this.saveSettings(this.#state.defaults);
  }

  #refusal() {
    const unit = this.#state.image;
    if (this.#product && this.#product !== productOf(unit)) return 'wrong_product';
    if (!this.#product && unit?.product) return 'unmarked_image';
    if (unit?.key && this.#key !== unit.key) return 'wrong_key';
    if (this.#running && !unit?.downgrade && compareVersions(this.#version, this.#running) < 0) return 'older';
    return null;
  }

  async #learn() {
    await this.#send({ command: 'update' });
    await this.#send({ command: 'status' });
    await this.#send({ command: 'get' });
    await this.#send({ command: 'defaults' });
    try {
      this.#packetBytes = await packetBudget(this.#smp);
      this.#running = parseVersion(await runningVersion(this.#smp));
    } catch (error) {
      return this.#fail(error);
    }
    this.#set({ running: this.#running && versionText(this.#running) });
  }

  #needsFull({ full, pin }) {
    const { image } = this.#state;
    return full === false && Boolean(image) && fullImageNeeded(image.imu, pin);
  }

  async #send(command) {
    try {
      await this.#device.sendConfig(command);
    } catch (error) {
      this.#fail(error);
    }
  }

  #ask(task, fields = {}) {
    this.#set({ phase: 'asking', task });
    return this.#send({ command: task, ...fields });
  }

  #receive(reply) {
    if (!reply) return;
    if (reply.command === 'update') this.#set({ image: imageOf(reply) });
    else if (reply.command === 'status') this.#set({ status: statusOf(reply) });
    else if (reply.command === 'config') this.#set({ settings: settingsOf(reply), ...identityOf(reply) });
    else if (reply.command === 'defaults') this.#set({ defaults: settingsOf(reply) });
    else if (reply.command === 'acknowledgement') this.#acked(reply);
  }

  #acked(reply) {
    const { phase, task } = this.#state;
    if (reply.reason === 'claimed') return this.#set({ phase: 'ready', task: null, notice: this.#noticeDuring(task, { key: 'claimed' }) });
    if (reply.reason === 'small_mtu') return this.#narrow();
    if (phase !== 'asking' && phase !== 'confirming' && phase !== 'uploading') return;
    if (reply.pending) return this.#set({ phase: 'confirming' });
    if (reply.acknowledge && task === 'set' && !reply.reason) return this.#saved();
    if (reply.acknowledge && reply.reason === task) return this.#granted(task);
    if (task === 'install') this.#windowOpen = false;
    const refusedOnDevice = this.#state.refusedOnDevice || reply.reason === 'refused_on_device' || (reply.reason === 'cancelled' && phase === 'confirming');
    this.#set({ phase: 'ready', task: null, refusedOnDevice, notice: this.#noticeDuring(task, { key: reply.reason || 'refused' }) });
  }

  #narrow() {
    const asked = this.#state.phase === 'asking' ? { phase: 'ready', task: null } : {};
    this.#set({ ...asked, notice: { key: 'small_mtu' } });
  }

  #granted(task) {
    if (task === 'dfu') {
      this.#windowOpen = true;
      return this.#upload();
    }
    this.#windowOpen = false;
    return this.#set({ phase: task === 'install' ? 'installing' : 'recovering', task: null });
  }

  #saved() {
    this.#set({ phase: 'ready', task: null, notice: { key: 'saved', during: 'set' } });
    return this.#send({ command: 'get' });
  }

  async #upload() {
    if (!this.#packetBytes) return this.#set({ notice: { key: 'no_params' } });
    const total = this.#image.length;
    this.#set({ phase: 'uploading', task: 'install', progress: { sent: 0, total } });
    try {
      await this.#measureWrites();
      await this.#sendImage(total);
    } catch (error) {
      if (error instanceof SmpError && error.rc === RC.accessDenied) this.#windowOpen = false;
      if (this.#state.phase === 'uploading') this.#fail(error);
      return;
    }
    // INFO: fc 03oct26 the device's verdict can land before the reply to the last chunk
    if (this.#state.phase === 'uploading') this.#set({ phase: 'asking' });
  }

  async #measureWrites() {
    if (this.#probed) return;
    try {
      this.#writeBytes = await probeWriteBytes(this.#smp, this.#packetBytes);
    } catch (error) {
      if (error instanceof LinkLost) throw error;
      this.#writeBytes = GATT_WRITE_BYTES;
    }
    this.#probed = true;
  }

  async #sendImage(total) {
    for (;;) {
      try {
        return await upload(this.#smp, this.#image, this.#sha, this.#uploadPacketBytes(), sent => {
          this.#set({ progress: { sent, total } });
        });
      } catch (error) {
        const smaller = SMALLER_WRITES.find(size => size < this.#writeBytes);
        if (!(error instanceof SmpWriteRejected) || !this.#device || !smaller) throw error;
        this.#writeBytes = smaller;
      }
    }
  }

  #uploadPacketBytes() {
    const oneWrite = this.#writeBytes > GATT_WRITE_BYTES && dataRoom(this.#image, this.#sha, 0, this.#writeBytes) > 0;
    return oneWrite ? Math.min(this.#packetBytes, this.#writeBytes) : this.#packetBytes;
  }

  #fail(error) {
    this.#set({ phase: this.#device ? 'ready' : 'offline', task: null, notice: this.#noticeDuring(this.#state.task, this.#noticeOf(error)) });
  }

  #noticeDuring(task, notice) {
    return task === 'set' ? { ...notice, during: 'set' } : notice;
  }

  #noticeOf(error) {
    const notice = noticeOf(error);
    return notice.key === 'wrong_product' && !this.#product ? { key: 'unmarked_image' } : notice;
  }

  #closed() {
    const { phase } = this.#state;
    const leaving = this.#leaving;
    this.#device = null;
    this.#windowOpen = false;
    this.#running = null;
    this.#packetBytes = null;
    this.#writeBytes = GATT_WRITE_BYTES;
    this.#probed = false;
    this.#smp.close(new LinkLost());
    if (EXPECTED_DROP.has(phase)) {
      const next = phase === 'recovering' ? 'recovering' : 'rebooting';
      return this.#set({ phase: next, running: null, status: null, settings: null, address: null, defaults: null, refusedOnDevice: false, progress: null });
    }
    const notice = BUSY.has(phase) && !leaving ? { key: 'link_lost' } : this.#state.notice;
    this.#set({
      phase: 'offline', task: null, device: null, running: null, status: null, settings: null, address: null, defaults: null, refusedOnDevice: false,
      progress: null,
      notice,
    });
  }

  #set(patch) {
    const state = { ...this.#state, ...patch };
    this.#state = { ...state, advice: adviceOf(state), settling: settlingOf(state) };
    this.#onChange(this.#state);
  }
}

const sharedListeners = new Set();
let sharedUpdater = null;

// INFO: fc 11nov26 one link for the update and settings pages, which Turbo swaps without reloading this module
export function joinSession(onChange) {
  sharedUpdater ??= new Updater({ onChange: state => sharedListeners.forEach(listener => listener(state)) });
  sharedListeners.add(onChange);
  return { updater: sharedUpdater, leave: () => sharedListeners.delete(onChange) };
}
