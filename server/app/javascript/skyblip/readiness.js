export function onDesktopLinux(navigator) {
  const platform = navigator?.userAgentData?.platform;
  if (platform !== undefined) return platform === 'Linux';
  const agent = navigator?.userAgent ?? '';
  return /Linux/.test(agent) && !/Android|CrOS/.test(agent);
}

export function onChromium(navigator) {
  return Boolean(navigator?.userAgentData?.brands?.some(({ brand }) => brand === 'Chromium'));
}

function hasUsbInstall(scope = globalThis) {
  return Boolean(scope.navigator?.serial && typeof scope.showDirectoryPicker === 'function');
}

// INFO: fc 25nov26 Samsung Internet on Android has both APIs, yet its port chooser closes empty at once, so a phone or tablet never installs
export function browserSupport(scope = globalThis) {
  const agent = scope.navigator?.userAgentData;
  const mobile = Boolean(agent?.mobile) || ['Android', 'iOS'].includes(agent?.platform) || /Android|iPhone|iPad/.test(scope.navigator?.userAgent ?? '');
  if (mobile) return 'phone';
  return hasUsbInstall(scope) ? 'usb' : 'other';
}

// INFO: fc 24nov26 Chrome ships Web Bluetooth on Linux behind a flag, and getAvailability() says only whether an adapter is present
export class BluetoothReadiness {
  #navigator;
  #adapter = 'unknown';
  #listeners = new Set();
  #watching = false;

  constructor(navigator = globalThis.navigator) {
    this.#navigator = navigator;
  }

  get checks() {
    const bluetooth = this.#bluetooth;
    const chromium = onChromium(this.#navigator);
    const flagged = chromium && onDesktopLinux(this.#navigator);
    return {
      flag: flagged ? (bluetooth ? 'ok' : 'missing') : null,
      adapter: bluetooth ? this.#adapter : flagged ? 'unknown' : null,
      remember: !chromium ? null : !bluetooth ? 'unknown' : typeof bluetooth.getDevices === 'function' ? 'ok' : 'missing',
    };
  }

  get linux() {
    return onDesktopLinux(this.#navigator);
  }

  get ready() {
    return Boolean(this.#bluetooth) && this.#adapter !== 'missing';
  }

  get fixable() {
    const { flag, adapter } = this.checks;
    return flag === 'missing' || adapter === 'missing';
  }

  listen(listener) {
    this.#listeners.add(listener);
    this.#watch();
    return () => this.#listeners.delete(listener);
  }

  get #bluetooth() {
    return this.#navigator?.bluetooth ?? null;
  }

  #watch() {
    const bluetooth = this.#bluetooth;
    if (this.#watching || !bluetooth) return;
    this.#watching = true;
    bluetooth.addEventListener?.('availabilitychanged', event => this.#heard(event.value));
    bluetooth.getAvailability?.().then(
      available => this.#heard(available),
      error => console.warn('The browser did not say whether a Bluetooth adapter is present:', error.message),
    );
  }

  #heard(available) {
    const adapter = available ? 'ok' : 'missing';
    if (adapter === this.#adapter) return;
    this.#adapter = adapter;
    for (const listener of this.#listeners) listener(this);
  }
}

let shared = null;

export function bluetoothReadiness() {
  shared ??= new BluetoothReadiness();
  return shared;
}
