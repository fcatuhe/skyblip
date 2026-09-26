import { GATT_WRITE_BYTES, connect, hasWebBluetooth } from './ble.js';
import { compareVersions, parseVersion, readImage, sha256, versionText } from './image.js';
import {
  GROUP, RC, SmpClient, SmpError, SmpTimeout, SmpWriteRejected,
  dataRoom, packetBudget, probeWriteBytes, runningVersion, upload,
} from './smp.js';

export { hasWebBluetooth };

const IMAGE_REFUSALS = {
  10: 'flash',
  11: 'flash',
  12: 'flash',
  13: 'flash',
  22: 'not_image',
  23: 'not_image',
  27: 'not_newer',
  30: 'too_large',
};

// INFO: fc 26sep26 the ATT payloads of MTU 247 and 185, where Android and iOS commonly settle
const SMALLER_WRITES = [244, 182, GATT_WRITE_BYTES];

const DROPPED_UPLOAD = new Set(['nothing_staged', 'upload_unfinished']);
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
  };
}

function statusOf(reply) {
  return {
    flight: reply.flight ?? null,
    upload: reply.upload === true,
    batteryPercent: reply.battery_valid === false ? null : reply.battery_percent ?? null,
    charging: reply.charging === true,
    powerLevel: reply.power_level ?? null,
    wentDarkFlat: reply.went_dark_flat === true,
  };
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
  #windowOpen = false;
  #uploaded = false;
  #leaving = false;
  #state = {
    phase: 'offline',
    task: null,
    device: null,
    image: null,
    running: null,
    status: null,
    file: null,
    progress: null,
    notice: null,
  };

  constructor({ onChange }) {
    this.#onChange = onChange;
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

  async choose(bytes, name) {
    const image = readImage(bytes);
    this.#uploaded = false;
    if (!image) {
      this.#image = null;
      return this.#set({ file: null, notice: { key: 'not_image' } });
    }
    this.#image = bytes;
    this.#sha = await sha256(bytes);
    this.#version = image.version;
    this.#set({ file: { name, version: versionText(image.version), bytes: bytes.length }, notice: null });
  }

  async install() {
    if (!this.#image) return this.#set({ notice: { key: 'no_file' } });
    if (this.#running && compareVersions(this.#version, this.#running) <= 0) {
      return this.#set({ notice: { key: 'not_newer' } });
    }
    this.#set({ notice: null });
    if (this.#uploaded) return this.#ask('apply');
    if (this.#windowOpen) return this.#upload();
    return this.#ask('dfu');
  }

  recover() {
    this.#set({ notice: null });
    return this.#ask('recovery');
  }

  refresh() {
    return this.#send({ cmd: 'update' });
  }

  async #learn() {
    await this.#send({ cmd: 'update' });
    await this.#send({ cmd: 'status' });
    try {
      this.#packetBytes = await packetBudget(this.#smp);
      this.#running = parseVersion(await runningVersion(this.#smp));
    } catch (error) {
      return this.#fail(error);
    }
    this.#set({ running: this.#running && versionText(this.#running) });
  }

  async #send(command) {
    try {
      await this.#device.sendConfig(command);
    } catch (error) {
      this.#fail(error);
    }
  }

  #ask(task) {
    this.#set({ phase: 'asking', task });
    return this.#send({ cmd: task });
  }

  #receive(reply) {
    if (!reply) return;
    if (reply.cmd === 'update') this.#set({ image: imageOf(reply) });
    else if (reply.cmd === 'status') this.#set({ status: statusOf(reply) });
    else if ('ack' in reply) this.#acked(reply);
  }

  #acked(reply) {
    const { phase, task } = this.#state;
    if (reply.reason === 'claimed') return this.#set({ phase: 'ready', task: null, notice: { key: 'claimed' } });
    if (phase !== 'asking' && phase !== 'confirming') return;
    if (reply.pending) return this.#set({ phase: 'confirming' });
    if (reply.ack && reply.reason === task) return this.#granted(task);
    if (task === 'apply' && DROPPED_UPLOAD.has(reply.reason)) this.#uploaded = false;
    this.#set({ phase: 'ready', task: null, notice: { key: reply.reason || 'refused' } });
  }

  #granted(task) {
    if (task === 'dfu') {
      this.#windowOpen = true;
      return this.#upload();
    }
    this.#uploaded = false;
    this.#windowOpen = false;
    return this.#set({ phase: task === 'apply' ? 'installing' : 'recovering', task: null });
  }

  async #upload() {
    if (!this.#packetBytes) return this.#set({ notice: { key: 'no_params' } });
    const total = this.#image.length;
    this.#set({ phase: 'uploading', task: null, progress: { sent: 0, total } });
    try {
      await this.#measureWrites();
      await this.#sendImage(total);
    } catch (error) {
      if (error instanceof SmpError && error.rc === RC.accessDenied) this.#windowOpen = false;
      return this.#fail(error);
    }
    this.#uploaded = true;
    await this.#ask('apply');
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
    this.#set({ phase: this.#device ? 'ready' : 'offline', task: null, notice: noticeOf(error) });
  }

  #closed() {
    const { phase } = this.#state;
    const leaving = this.#leaving;
    this.#device = null;
    this.#windowOpen = false;
    this.#uploaded = false;
    this.#running = null;
    this.#packetBytes = null;
    this.#writeBytes = GATT_WRITE_BYTES;
    this.#probed = false;
    this.#smp.close(new LinkLost());
    if (EXPECTED_DROP.has(phase)) {
      const next = phase === 'recovering' ? 'recovering' : 'rebooting';
      return this.#set({ phase: next, running: null, status: null, progress: null });
    }
    const notice = BUSY.has(phase) && !leaving ? { key: 'link_lost' } : this.#state.notice;
    this.#set({ phase: 'offline', task: null, device: null, running: null, status: null, progress: null, notice });
  }

  #set(patch) {
    this.#state = { ...this.#state, ...patch };
    this.#onChange(this.#state);
  }
}
