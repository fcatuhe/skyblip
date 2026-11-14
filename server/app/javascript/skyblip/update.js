import { GATT_WRITE_BYTES, addressOf, connect, hasWebBluetooth } from '#skyblip/ble';
import { compareVersions, fullImageNeeded, parseVersion, readImage, sha256, versionText } from '#skyblip/image';
import { installReportOf, landingOf } from '#skyblip/install_report';
import { hex } from '#skyblip/link';
import { LogReader, fetchDiagnostics } from '#skyblip/log';
import { changedSettings, settingsFrame, settingsOf, statusOf } from '#skyblip/settings';
import {
  GROUP, RC, SmpClient, SmpError, SmpTimeout, SmpWriteRejected,
  dataRoom, packetBudget, probeWriteBytes, runningVersion, upload,
} from '#skyblip/smp';
import { BrowserTrust, storedBrowserKey } from '#skyblip/trust';

export { hasWebBluetooth };

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

export const LINKED_PHASES = new Set(['ready', 'asking', 'confirming', 'uploading', 'installing']);
// INFO: fc 11nov26 an install restarts the device in about thirty seconds, and a full image writes the hub for up to a minute more
const RETURN_WITHIN_MS = 120_000;
export const RETURN_EVERY_MS = 3_000;
const EXPECTED_DROP = new Set(['installing', 'rebooting', 'recovering']);
const BUSY = new Set(['asking', 'confirming', 'uploading']);
const NOTED_TASKS = new Set(['set', 'capture']);
const LANDING_REFUSALS = new Set(['nothing_staged', 'not_approved', 'wrong_key']);
export const PROTOCOL_NAMES = { adsb: 'ADS-B', adsl: 'ADS-L', alptas: 'ALP-TAS' };

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

export function protocolsOf(reply) {
  return Object.entries(PROTOCOL_NAMES)
    .filter(([stem]) => typeof reply[`${stem}_version`] === 'string')
    .map(([stem, name]) => ({ name, version: reply[`${stem}_version`] }));
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

function upgradeOf({ file, running }) {
  if (!file || !running) return null;
  return compareVersions(parseVersion(file.version), parseVersion(running)) > 0;
}

// INFO: fc 15nov26 the dump's capture group, absent on firmware from before it and on a unit with no capture flash
export function captureOf(dump) {
  const group = dump?.capture;
  if (!group) return null;
  return {
    armed: group.armed === true,
    profile: group.profile ?? null,
    session: group.session ?? 0,
    records: group.records ?? 0,
    keeps: group.keeps_s ?? 0,
    takesSectors: group.takes_sectors ?? 0,
    evictsFlights: group.evicts_flights ?? 0,
    keepsFull: group.keeps_full_s ?? 0,
    keepsPowerRun: group.keeps_power_run_s ?? 0,
  };
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
  #answersReport = false;
  #running = null;
  #image = null;
  #sha = null;
  #version = null;
  #key = null;
  #product = null;
  #captureProfile = null;
  #log = null;
  #dump = {};
  #dumpAsked = false;
  #dumpPending = false;
  #windowOpen = false;
  #leaving = false;
  #choice = 0;
  #opening = null;
  #bluetoothDevice;
  #returnEveryMs;
  #returnWithinMs;
  #returnTimer = null;
  #trust;
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
    upgrade: null,
    settling: false,
    progress: null,
    notice: null,
    trust: null,
    browsers: null,
    installReport: null,
    landing: null,
    protocols: null,
    dump: null,
    capture: null,
    fetching: null,
  };

  constructor({ onChange, device = null, returnEveryMs = null, returnWithinMs = RETURN_WITHIN_MS, browserKey = storedBrowserKey }) {
    this.#onChange = onChange;
    this.#trust = new BrowserTrust({ send: command => this.#device.sendConfig(command), loadKey: browserKey, onChange: patch => this.#set(patch) });
    this.#bluetoothDevice = device;
    this.#returnEveryMs = returnEveryMs;
    this.#returnWithinMs = returnWithinMs;
  }

  get state() {
    return this.#state;
  }

  get bluetoothDevice() {
    return this.#bluetoothDevice;
  }

  get inFlight() {
    return this.#state.status?.flight === 'airborne';
  }

  async connect() {
    if (!hasWebBluetooth()) return this.#set({ notice: { key: 'no_bluetooth' } });
    // INFO: fc 11nov26 a second client on the same link would hear every reply twice, and an install would fail on it
    if (this.#device || this.#state.phase === 'connecting') return;
    if (this.#opening) return this.#opening;
    this.#stopReturn();
    this.#leaving = false;
    this.#set({ phase: 'connecting', notice: null });
    try {
      await this.#open();
    } catch (error) {
      return this.#set({ phase: 'offline', notice: error.name === 'NotFoundError' ? null : noticeOf(error) });
    }
  }

  async #open() {
    this.#opening ??= this.#link().finally(() => (this.#opening = null));
    return this.#opening;
  }

  async #link() {
    const device = await connect({
      device: this.#bluetoothDevice,
      onReply: reply => this.#receive(reply),
      onLog: (frame, bytes, error) => this.#log?.receive(frame, bytes, error),
      onSmp: bytes => this.#smp.receive(bytes),
      onClose: () => this.#closed(),
    });
    this.#bluetoothDevice = device.device;
    if (this.#leaving && this.#state.phase !== 'connecting') {
      device.release();
      return this.#set({ phase: 'offline' });
    }
    if (!device.hasConfig || !device.hasSmp) {
      this.#leaving = true;
      device.disconnect();
      return this.#set({ phase: 'offline', notice: { key: 'not_skyblip' } });
    }
    this.#device = device;
    this.#log = device.hasLog ? new LogReader(fields => device.sendLog(fields)) : null;
    await this.#trust.prove();
    this.#set({ phase: 'ready', device: device.name });
    await this.#learn();
  }

  #awaitReturn() {
    const deadline = performance.now() + this.#returnWithinMs;
    const attempt = async () => {
      this.#returnTimer = null;
      if (this.#state.phase !== 'rebooting') return;
      try {
        return await this.#open();
      } catch {
        if (this.#state.phase !== 'rebooting') return;
        if (performance.now() < deadline) this.#returnTimer = setTimeout(attempt, this.#returnEveryMs);
        else this.#set({ phase: 'offline', notice: { key: 'not_back' } });
      }
    };
    this.#returnTimer = setTimeout(attempt, this.#returnEveryMs);
  }

  #stopReturn() {
    clearTimeout(this.#returnTimer);
    this.#returnTimer = null;
  }

  disconnect() {
    this.#leaving = true;
    if (this.#device) return this.#device.disconnect();
    this.#stopReturn();
    if (this.#state.phase === 'rebooting') this.#set({ phase: 'offline' });
  }

  clearImage() {
    this.#choice++;
    this.#image = null;
    this.#set({ file: null, notice: null });
  }

  async choose(bytes, name) {
    const image = readImage(bytes);
    const choice = ++this.#choice;
    if (!image) {
      this.#image = null;
      return this.#set({ file: null, notice: { key: 'not_image' } });
    }
    const sha = await sha256(bytes);
    if (choice !== this.#choice) return;
    this.#image = bytes;
    this.#sha = sha;
    this.#version = image.version;
    const file = { name, version: versionText(image.version), bytes: bytes.length, key: image.key, sha: hex(sha.subarray(0, 8)), ...image.imu };
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
    if (this.inFlight && !this.#trust.trusted) return this.#set({ notice: { key: 'in_flight', during: 'set' } });
    this.#set({ notice: null });
    return this.#ask('set', settingsFrame(changed));
  }

  resetSettings() {
    if (this.#state.defaults) return this.saveSettings(this.#state.defaults);
  }

  // INFO: fc 15nov26 firmware from before a Go served capture answers it unknown_command there
  orderCapture(profile) {
    if (this.inFlight) return this.#set({ notice: { key: 'in_flight', during: 'capture' } });
    this.#captureProfile = profile;
    this.#set({ notice: null });
    return this.#ask('capture', { profile });
  }

  // INFO: fc 15nov26 a narrow link answers a dump too wide for it small_mtu, which no pilot asked about
  readDiagnostics() {
    this.#dumpAsked = true;
    this.#dumpPending = true;
    return this.#send({ command: 'diagnostics' });
  }

  // INFO: fc 15nov26 the last session of the diagnostics store, which is the capture the page armed
  async fetchCapture() {
    if (this.#state.phase !== 'ready' || !this.#log) return null;
    this.#set({ phase: 'asking', task: 'fetch', fetching: { at: 0, total: null }, notice: null });
    try {
      const fetched = await fetchDiagnostics(this.#log, { onProgress: (at, total) => this.#set({ fetching: { at, total } }) });
      if (this.#state.task === 'fetch') this.#set({ phase: 'ready', task: null, fetching: null });
      return fetched;
    } catch (error) {
      if (this.#state.task === 'fetch') this.#set({ phase: this.#device ? 'ready' : 'offline', task: null, fetching: null, notice: { key: error.reason ?? 'failed', detail: error.message, during: 'capture' } });
      return null;
    }
  }

  // INFO: fc 13nov26 the unit's replies carry no request id, so the list waits for the link to be quiet
  listBrowsers() {
    return this.#browsing(() => this.#trust.list());
  }

  forgetBrowsers(ids) {
    return this.#browsing(async () => {
      this.#set({ notice: null });
      const refusal = await this.#trust.forget(ids);
      if (typeof refusal === 'string') this.#set({ notice: { key: refusal, during: 'browsers' } });
    });
  }

  async #browsing(exchange) {
    if (this.#state.phase !== 'ready' || !this.#trust.trusted) return;
    this.#set({ phase: 'asking', task: 'browsers' });
    await exchange();
    if (this.#state.task === 'browsers') this.#set({ phase: 'ready', task: null });
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
    await this.#send({ command: 'install_report' });
    await this.#send({ command: 'protocols' });
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
    await this.listBrowsers();
  }

  #needsFull({ full, pin }) {
    const { image } = this.#state;
    return full === false && Boolean(image) && fullImageNeeded(image.imu, pin);
  }

  async #send(command) {
    if (!this.#device) return this.#set({ phase: 'offline', task: null, notice: this.#noticeDuring(this.#state.task, { key: 'link_lost' }) });
    try {
      await this.#device.sendConfig(command);
    } catch (error) {
      this.#fail(error);
    }
  }

  #ask(task, fields = {}) {
    const asked = { command: task, ...fields };
    this.#set({ phase: 'asking', task, asked });
    return this.#send(asked);
  }

  #receive(reply) {
    if (!reply || this.#trust.receive(reply)) return;
    if (reply.command === 'update') this.#set({ image: imageOf(reply) });
    else if (reply.command === 'status') this.#set({ status: statusOf(reply) });
    else if (reply.command === 'config') this.#configured(reply);
    else if (reply.command === 'defaults') this.#set({ defaults: settingsOf(reply) });
    else if (reply.command === 'install_report') this.#reported(reply);
    else if (reply.command === 'protocols') this.#set({ protocols: protocolsOf(reply) });
    else if (reply.command === 'diagnostics') this.#dumped(reply);
    else if (reply.command === 'acknowledgement') this.#acked(reply);
  }

  // INFO: fc 15nov26 the dump is asked once the settings are answered, so a small_mtu after it is the dump's own
  #configured(reply) {
    this.#set({ settings: settingsOf(reply), ...identityOf(reply) });
    if (!this.#dumpAsked) this.readDiagnostics();
  }

  // INFO: fc 15nov26 one frame per group, or more for a group wider than the link, and more false on the last
  #dumped({ command, group, part, more, ...fields }) {
    this.#dump = { ...this.#dump, [group]: { ...this.#dump[group], ...fields } };
    if (more) return;
    const dump = this.#dump;
    this.#dump = {};
    this.#dumpPending = false;
    this.#set({ dump, capture: captureOf(dump) });
  }

  #reported(reply) {
    this.#answersReport = true;
    this.#set({ installReport: installReportOf(reply), landing: landingOf(reply) });
  }

  #acked(reply) {
    const { phase, task } = this.#state;
    if (reply.reason === 'claimed') return this.#set({ phase: 'ready', task: null, notice: this.#noticeDuring(task, { key: 'claimed' }) });
    if (this.#dumpPending && ['small_mtu', 'no_statistics'].includes(reply.reason)) return (this.#dumpPending = false);
    if (reply.reason === 'small_mtu') return this.#narrow();
    if (phase !== 'asking' && phase !== 'confirming' && phase !== 'uploading') return;
    if (reply.pending) return this.#set({ phase: 'confirming' });
    if (reply.acknowledge && task === 'set' && !reply.reason) return this.#saved();
    if (reply.acknowledge && reply.reason === task) return this.#granted(task);
    if (task === 'install') this.#windowOpen = false;
    const refusedOnDevice = this.#state.refusedOnDevice || reply.reason === 'refused_on_device' || (reply.reason === 'cancelled' && phase === 'confirming');
    this.#set({ phase: 'ready', task: null, refusedOnDevice, notice: this.#noticeDuring(task, { key: reply.reason || 'refused' }) });
    // INFO: fc 14nov26 firmware without the report answers unknown_command, which would read as the refusal of the next ask
    if (task === 'install' && LANDING_REFUSALS.has(reply.reason) && this.#answersReport) return this.#send({ command: 'install_report' });
  }

  #narrow() {
    const asked = this.#state.phase === 'asking' ? { phase: 'ready', task: null } : {};
    this.#set({ ...asked, notice: { key: 'small_mtu' } });
    if (!this.#dumpAsked) this.readDiagnostics();
  }

  #granted(task) {
    if (task === 'capture') {
      const key = this.#captureProfile === 'stop' ? 'capture_stopped' : 'capture_armed';
      this.#set({ phase: 'ready', task: null, notice: { key, during: 'capture' } });
      return this.readDiagnostics();
    }
    if (task === 'dfu') {
      this.#trust.confirmed();
      this.#windowOpen = true;
      return this.#upload();
    }
    this.#windowOpen = false;
    return this.#set({ phase: task === 'install' ? 'installing' : 'recovering', task: null });
  }

  async #saved() {
    const trusting = !this.#trust.trusted;
    this.#trust.confirmed();
    this.#set({ phase: 'ready', task: null, notice: { key: 'saved', during: 'set' } });
    await this.#send({ command: 'get' });
    if (trusting) await this.listBrowsers();
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
    return NOTED_TASKS.has(task) ? { ...notice, during: task } : notice;
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
    this.#answersReport = false;
    this.#log?.close();
    this.#log = null;
    this.#dump = {};
    this.#dumpAsked = false;
    this.#dumpPending = false;
    this.#smp.close(new LinkLost());
    this.#trust.close();
    if (EXPECTED_DROP.has(phase)) {
      const next = phase === 'recovering' ? 'recovering' : 'rebooting';
      this.#set({ phase: next, running: null, status: null, settings: null, address: null, defaults: null, refusedOnDevice: false, progress: null, installReport: null, landing: null, protocols: null, dump: null, capture: null, fetching: null });
      if (next === 'rebooting' && this.#returnEveryMs !== null && !leaving) this.#awaitReturn();
      return;
    }
    const notice = BUSY.has(phase) && !leaving ? { key: 'link_lost' } : this.#state.notice;
    this.#set({
      phase: 'offline', task: null, device: null, running: null, status: null, settings: null, address: null, defaults: null, refusedOnDevice: false,
      progress: null, installReport: null, landing: null, protocols: null, dump: null, capture: null, fetching: null,
      notice,
    });
  }

  #set(patch) {
    const state = { ...this.#state, ...patch };
    this.#state = { ...state, advice: adviceOf(state), upgrade: upgradeOf(state), settling: settlingOf(state) };
    this.#onChange(this.#state);
  }
}
