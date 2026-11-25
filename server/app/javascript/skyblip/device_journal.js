import { installCause, reportShown } from '#skyblip/install_report';
import { LINKED_PHASES, productOf } from '#skyblip/update';

const TASKS = new Set(['dfu', 'install', 'recovery', 'set', 'capture']);
const EXPECTED_DROP = new Set(['rebooting', 'recovering']);
const IMU_STATES = new Set(['none', 'missing', 'corrupt', 'unreadable', 'writing']);
// INFO: fc 19nov26 the HCI reasons a unit's controller gives a dropped link, the central being the phone or a paired unit
const DROP_REASONS = { 0x08: 'timeout', 0x13: 'central_closed', 0x14: 'central_low_resources', 0x15: 'central_powered_off', 0x16: 'unit_closed', 0x22: 'll_timeout', 0x3d: 'mic_failure', 0x3e: 'not_established' };

const linked = state => LINKED_PHASES.has(state?.phase);
const sameNotice = (a, b) => a?.key === b?.key && a?.during === b?.during && a?.detail === b?.detail;
const taskOf = task => (task === 'dfu' ? 'install' : task);

// INFO: fc 14nov26 the updater hands over states, not events, so the witness keeps what it needs to tell what changed between two
export class DeviceWitness {
  #now;
  #before = null;
  #connectingSince = null;
  #linkedSince = null;
  #task = null;
  #install = null;
  #returning = false;
  #read = false;
  #dropRead = false;

  constructor({ now = () => performance.now() } = {}) {
    this.#now = now;
  }

  witness(state, { attempted = null } = {}) {
    const before = this.#before;
    this.#before = state;
    return [
      ...this.#link(before, state),
      ...this.#tasks(before, state),
      ...this.#notice(before, state),
      ...this.#landing(before, state),
      ...this.#learned(state, attempted),
      ...this.#lastDrop(state),
    ];
  }

  #link(before, state) {
    const now = this.#now();
    if (state.phase === 'connecting' && before?.phase !== 'connecting') this.#connectingSince = now;
    if (linked(state) && !linked(before)) {
      const returned = before?.phase === 'rebooting';
      const connectMs = this.#connectingSince === null ? null : Math.round(now - this.#connectingSince);
      this.#connectingSince = null;
      this.#linkedSince = now;
      this.#returning = returned;
      this.#read = false;
      this.#dropRead = false;
      return [['device.connected', { connect_ms: returned ? null : connectMs, returned }]];
    }
    if (linked(before) && !linked(state)) {
      const linkedMs = this.#linkedSince === null ? null : Math.round(now - this.#linkedSince);
      this.#linkedSince = null;
      return [['device.disconnected', { phase: before.phase, expected: EXPECTED_DROP.has(state.phase) || state.left === true, linked_ms: linkedMs }]];
    }
    return [];
  }

  #tasks(before, state) {
    const entries = [];
    const was = TASKS.has(before?.task) ? before.task : null;
    const is = TASKS.has(state.task) ? state.task : null;
    if (this.#task && state.phase === 'confirming') this.#task.confirmed = true;
    if (was === is || (was === 'dfu' && is === 'install')) return entries;
    if (was && this.#task) entries.push(this.#ended(before, state));
    if (is) entries.push(this.#started(state));
    return entries;
  }

  #started(state) {
    const task = taskOf(state.task);
    this.#task = { task, since: this.#now(), confirmed: false };
    if (task !== 'install') return ['device.task_started', { task }];
    this.#install = { since: this.#now(), from: state.running };
    const { file, advice, upgrade } = state;
    return ['device.task_started', { task, version: file?.version, full: file?.full, key: file?.key, sha: file?.sha, advice, upgrade }];
  }

  #ended(before, state) {
    const { task, since, confirmed } = this.#task;
    this.#task = null;
    const progress = task === 'install' ? before.progress : null;
    return ['device.task_ended', {
      task, outcome: state.notice?.key ?? 'granted', lasted_ms: Math.round(this.#now() - since), confirmed,
      sent_bytes: progress?.sent, total_bytes: progress?.total,
    }];
  }

  #notice(before, state) {
    if (!state.notice || sameNotice(state.notice, before?.notice)) return [];
    const { key, during, detail } = state.notice;
    return [['device.noticed', { key, during, detail, phase: state.phase, task: taskOf(before?.task ?? state.task) }]];
  }

  #landing(before, state) {
    const { landing, notice, file, image } = state;
    if (!landing || landing === before?.landing) return [];
    return [['device.upload_refused', {
      reason: notice?.key, version: file?.version, check: landing.check, staged_key: landing.stagedKey, key: file?.key, unit_key: image?.key,
    }]];
  }

  #learned(state, attempted) {
    if (!linked(state) || !state.running || !state.image) return [];
    const entries = [];
    if (!this.#read) {
      this.#read = true;
      entries.push(['device.read', readOf(state)]);
      const report = reportOf(state, this.#returning ? attempted : null);
      if (report) entries.push(['device.install_reported', report]);
    }
    if (this.#returning && this.#install && attempted && state.running === attempted && state.image.state === 'confirmed') {
      entries.push(['device.installed', {
        from: this.#install.from, to: state.running, lasted_ms: Math.round(this.#now() - this.#install.since), device_id: state.identity?.deviceId,
      }]);
      this.#install = null;
      this.#returning = false;
    }
    return entries;
  }

  #lastDrop(state) {
    if (!linked(state) || !state.dump || this.#dropRead) return [];
    this.#dropRead = true;
    const code = state.dump.system?.link_drop_reason;
    return code ? [['device.drop_reported', { reason: DROP_REASONS[code] ?? 'other', code }]] : [];
  }
}

function readOf(state) {
  const { image, identity, running, trust, serial, status } = state;
  const product = productOf(state);
  return {
    product, variant: identity?.variant, version: identity?.version, release: identity?.release, device_id: identity?.deviceId,
    bluetooth_tag: identity?.bluetoothTag, running, image: image.state, key: image.key,
    track: image.key ? (image.downgrade ? 'development' : 'release') : null,
    bootloader_keys: image.bootloaderKeys?.join(' ') ?? null,
    imu: IMU_STATES.has(image.imu) ? image.imu : image.imu ? 'held' : null,
    settings: image.settings, swap_powered: image.swapPowered, trust,
    activated: product === 'go' ? serial !== null : null,
    flight: status?.flight, battery_percent: status?.batteryPercent, charging: status?.charging, power_level: status?.powerLevel,
  };
}

function reportOf(state, attempted) {
  const { image, installReport: report, running } = state;
  const cause = installCause({ image, report, running, attempted });
  if (!reportShown({ image, cause })) return null;
  return {
    cause, product: productOf(state), running, tried: attempted, image: image?.state, from: image?.from, to: image?.to,
    report: Boolean(report), check: report?.check, swap: report?.swap, request_error: report?.requestError,
    boots: report?.boots, probation_boots: report?.probationBoots, proof: report?.proof, reset: report?.reset,
  };
}

export class ActivationWitness {
  #now;
  #before = null;
  #since = null;

  constructor({ now = () => performance.now() } = {}) {
    this.#now = now;
  }

  witness(state) {
    const before = this.#before;
    this.#before = state;
    const entries = [];
    if (state.phase !== before?.phase) {
      const now = this.#now();
      entries.push(['activation.stepped', { phase: state.phase, previous: before?.phase, lasted_ms: this.#since === null ? null : Math.round(now - this.#since) }]);
      this.#since = now;
    }
    if (state.notice && !sameNotice(state.notice, before?.notice)) {
      entries.push(['activation.noticed', { key: state.notice.key, detail: state.notice.detail, phase: state.phase }]);
    }
    return entries;
  }
}
