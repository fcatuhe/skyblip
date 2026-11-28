import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { DRIVE_FILE, Installer, USB_VENDORS } from '#skyblip/installer';
import { LAYOUTS, RECORD_BYTES, parseUf2, readSpan, recordPage, trustRecord } from '#skyblip/uf2';
import { FakeNode } from './fake_meshtastic.mjs';
import { releaseUf2 } from './fake_uf2.mjs';

const T_ECHO_INFO = readFileSync(new URL('../../../firmware/boards/lilygo/t_echo_plus/factory/INFO_UF2.TXT', import.meta.url), 'utf8');
const X1_INFO = readFileSync(new URL('../../../firmware/boards/seeed/meshtracker_x1/factory/INFO_UF2.TXT', import.meta.url), 'utf8');
const KEY = { publicKey: Uint8Array.from({ length: 32 }, (_, at) => 255 - at), label: 'Chrome on Linux' };

function namedError(name, message = name) {
  return Object.assign(new Error(message), { name });
}

class FakePort {
  constructor(node, info = { usbVendorId: 0x239a, usbProductId: 0x4405 }) {
    this.node = node;
    this.info = info;
    this.opened = null;
    this.closed = false;
  }

  getInfo() {
    return this.info;
  }

  async open(options) {
    this.opened = options;
    this.readable = this.node.readable;
    this.writable = this.node.writable;
  }

  async close() {
    this.closed = true;
  }
}

class FakeDrive {
  constructor(info, { failWrite = false, restartIn = null } = {}) {
    this.files = new Map(info === null ? [] : [['INFO_UF2.TXT', new TextEncoder().encode(info)]]);
    Object.assign(this, { failWrite, restartIn, flashed: false });
  }

  async getFileHandle(name, { create = false } = {}) {
    if (!this.files.has(name) && !create) throw namedError('NotFoundError');
    const drive = this;
    let landed = new Uint8Array();
    const cut = () => namedError('InvalidStateError', 'An operation that depends on state cached in an interface object was made but the state had changed since it was read from disk.');
    return {
      getFile: async () => new Blob([drive.files.get(name)]),
      createWritable: async () => ({
        write: async bytes => {
          if (drive.failWrite) throw namedError('NoModificationAllowedError', 'The drive is read-only.');
          landed = Uint8Array.from([...landed, ...bytes]);
          drive.flashed = new DataView(landed.buffer).getUint32(24, true) * 512 === landed.length;
          if (drive.flashed && drive.restartIn === 'write') throw cut();
        },
        close: async () => {
          if (drive.flashed && drive.restartIn === 'close') throw cut();
          drive.files.set(name, landed);
        },
        abort: async () => {},
      }),
    };
  }
}

function installer({ node = new FakeNode(), drive = new FakeDrive(T_ECHO_INFO), key = KEY, port = null, release = null } = {}) {
  const states = [];
  const fakePort = port ?? new FakePort(node);
  const asked = [];
  const serial = { requestPort: async options => (asked.push(options), fakePort instanceof Error ? Promise.reject(fakePort) : fakePort) };
  const pickDrive = async () => (drive instanceof Error ? Promise.reject(drive) : drive);
  const download = async product => ({ bytes: (release ?? releaseUf2(product)).bytes, version: '0.1.0+1858' });
  const recorded = [];
  const journal = { record: (name, properties) => recorded.push([name, properties]) };
  const unit = new Installer({ onChange: state => states.push(state), serial, pickDrive, download, browserKey: async () => key, identifyWithinMs: 20, journal });
  return { unit, node, drive, port: fakePort, asked, states, recorded };
}

function written(drive) {
  return parseUf2(drive.files.get(DRIVE_FILE)).blocks;
}

test('a T-Echo running Meshtastic is named, restarted into its drive, and given skyBlip with this browser trusted', async () => {
  const { unit, node, drive, port, asked } = installer({ node: new FakeNode({ nodeNum: 0x9a3f2a, hwModel: 7 }) });
  await unit.restart();
  assert.deepEqual(asked, [{ filters: USB_VENDORS.map(usbVendorId => ({ usbVendorId })) }]);
  assert.deepEqual(port.opened, { baudRate: 115200 });
  assert.equal(node.restarted, true);
  assert.equal(port.closed, true);
  assert.deepEqual({ phase: unit.state.phase, product: unit.state.product, tag: unit.state.tag }, { phase: 'drive', product: 'go', tag: 0x3f2a });

  await unit.install();
  assert.deepEqual({ phase: unit.state.phase, board: unit.state.board, version: unit.state.version, trusted: unit.state.trusted },
    { phase: 'written', board: 'nRF52840-TEcho-v1', version: '0.1.0+1858', trusted: true });
  const blocks = written(drive);
  const page = recordPage(blocks, LAYOUTS.go);
  assert.deepEqual(readSpan(blocks, page, RECORD_BYTES), trustRecord(KEY));
});

test('an X1 gets the Bravo image, written at its own slot0', async () => {
  const { unit, drive } = installer({ node: new FakeNode({ hwModel: 128 }), drive: new FakeDrive(X1_INFO) });
  await unit.restart();
  await unit.install();
  assert.equal(unit.state.product, 'bravo');
  assert.equal(unit.state.phase, 'written');
  assert.ok(readSpan(written(drive), LAYOUTS.bravo.slot0, 4).every((byte, at) => byte === [0x3d, 0xb8, 0xf3, 0x96][at]));
});

test('a port that is not Meshtastic leads to the drive by hand, without a tag, for the product its USB vendor names', async () => {
  const { unit } = installer({ port: new FakePort(new FakeNode({ answers: false }), { usbVendorId: 0x2886, usbProductId: 0x1234 }) });
  await unit.restart();
  assert.deepEqual({ phase: unit.state.phase, drive: unit.state.drive, product: unit.state.product, tag: unit.state.tag, notice: unit.state.notice },
    { phase: 'drive', drive: 'gesture', product: 'bravo', tag: null, notice: null });
});

test('a unit already in its bootloader goes straight to its drive, and is never opened', async () => {
  const port = new FakePort(new FakeNode(), { usbVendorId: 0x239a, usbProductId: 0x0029 });
  const { unit, node } = installer({ port });
  await unit.restart();
  assert.deepEqual({ phase: unit.state.phase, drive: unit.state.drive, product: unit.state.product }, { phase: 'drive', drive: 'ready', product: 'go' });
  assert.equal(port.opened, null);
  assert.equal(node.restarted, false);
});

test('a T-Echo on SoftRF, which cannot restart into its drive, gets the gesture at once, with no wait', async () => {
  const port = new FakePort(new FakeNode(), { usbVendorId: 0x239a, usbProductId: 0x8029 });
  const { unit, recorded } = installer({ port });
  await unit.restart();
  assert.deepEqual({ phase: unit.state.phase, drive: unit.state.drive, product: unit.state.product }, { phase: 'drive', drive: 'gesture', product: 'go' });
  assert.equal(port.opened, null);
  assert.deepEqual(recorded.find(([name]) => name === 'installer.port_chosen'), ['installer.port_chosen', { usb_vendor: 0x239a, usb_product: 0x8029, firmware: 'softrf' }]);
});

test('an install leaves a trail of steps and facts, with no personal data', async () => {
  const { unit, recorded } = installer({ node: new FakeNode({ nodeNum: 0x9a3f2a, hwModel: 7 }) });
  await unit.restart();
  await unit.install();
  const facts = recorded.filter(([name]) => name !== 'installer.stepped');
  assert.deepEqual(facts.map(([name]) => name), ['installer.port_chosen', 'installer.identified', 'installer.drive_chosen', 'installer.wrote']);
  assert.deepEqual(facts[1][1], { product: 'go', hw_model: 7, version: '2.7.15.567b8ea' });
  assert.deepEqual(facts[2][1], { board: 'nRF52840-TEcho-v1', product: 'go', softdevice: 'S140 version 6.1.1' });
  assert.deepEqual({ ...facts[3][1], write_ms: 0 }, { product: 'go', version: '0.1.0+1858', trusted: true, write_ms: 0, detail: null });
  const steps = recorded.filter(([name]) => name === 'installer.stepped').map(([, { phase, previous }]) => `${previous}>${phase}`);
  assert.deepEqual(steps, ['idle>choosing_port', 'choosing_port>reading', 'reading>drive', 'drive>choosing_drive', 'choosing_drive>downloading', 'downloading>writing', 'writing>written']);
});

test('every notice the page shows is recorded with the step it came at', async () => {
  const { unit, recorded } = installer({ port: namedError('NotFoundError', 'No port selected by the user.') });
  await unit.restart();
  assert.deepEqual(recorded.at(-1), ['installer.noticed', { key: 'port_closed', detail: null, phase: 'idle' }]);
});

test('a Meshtastic board skyBlip does not run on is told so, and is not restarted', async () => {
  const { unit, node } = installer({ node: new FakeNode({ hwModel: 71 }) });
  await unit.restart();
  assert.equal(unit.state.phase, 'idle');
  assert.deepEqual(unit.state.notice, { key: 'other_board' });
  assert.equal(node.restarted, false);
});

test('closing the port chooser goes back to the start, with the cable hint', async () => {
  const { unit } = installer({ port: namedError('NotFoundError', 'No port selected by the user.') });
  await unit.restart();
  assert.deepEqual({ phase: unit.state.phase, notice: unit.state.notice }, { phase: 'idle', notice: { key: 'port_closed' } });
});

test('a port another program holds is said', async () => {
  const port = new FakePort(new FakeNode());
  port.open = async () => { throw namedError('NetworkError', 'Failed to open serial port.'); };
  const { unit } = installer({ port });
  await unit.restart();
  assert.equal(unit.state.notice.key, 'port_busy');
});

test('a drive that is not the bootloader is refused before anything is written', async () => {
  const { unit, drive } = installer({ drive: new FakeDrive(null) });
  await unit.restart();
  await unit.install();
  assert.deepEqual({ phase: unit.state.phase, notice: unit.state.notice }, { phase: 'drive', notice: { key: 'not_drive', detail: null } });
  assert.equal(drive.files.has(DRIVE_FILE), false);
});

test('a UF2 drive of another board is refused, and named', async () => {
  const { unit } = installer({ drive: new FakeDrive('Board-ID: nRF52840-Feather-v1\n') });
  await unit.restart();
  await unit.install();
  assert.deepEqual(unit.state.notice, { key: 'other_board', detail: 'nRF52840-Feather-v1' });
});

// A pilot's X1 stayed on its drive after the install, its bootloader swapped for SoftRF's, which runs S140 6.1.1
test('a bootloader on another SoftDevice than the image needs is refused before anything is written, and named', async () => {
  const cases = [
    [X1_INFO.replace('SoftDevice: S140 7.3.0', 'SoftDevice: S140 6.1.1'), 'S140 6.1.1'],
    [T_ECHO_INFO.replace('S140 version 6.1.1', 'S140 version 7.3.0'), 'S140 version 7.3.0'],
  ];
  for (const [info, softdevice] of cases) {
    const { unit, drive, recorded } = installer({ drive: new FakeDrive(info) });
    await unit.restart();
    await unit.install();
    assert.deepEqual({ phase: unit.state.phase, notice: unit.state.notice }, { phase: 'drive', notice: { key: 'other_softdevice', detail: softdevice } });
    assert.equal(drive.files.has(DRIVE_FILE), false);
    assert.equal(recorded.find(([name]) => name === 'installer.drive_chosen')[1].softdevice, softdevice);
  }
});

test('closing the folder chooser records a retryable drive notice, separate from denied permission', async () => {
  const closed = installer({ drive: namedError('AbortError', 'The user aborted a request.') });
  await closed.unit.restart();
  await closed.unit.install();
  assert.deepEqual(closed.unit.state.notice, { key: 'drive_closed' });
  assert.deepEqual(closed.recorded.at(-1), ['installer.noticed', { key: 'drive_closed', detail: null, phase: 'drive' }]);

  const denied = installer({ drive: namedError('NotAllowedError', 'Permission denied.') });
  await denied.unit.restart();
  await denied.unit.install();
  assert.equal(denied.unit.state.notice.key, 'drive_denied');
});

test('a browser with no key writes the release as published, and the unit will ask for its presses', async () => {
  const release = releaseUf2('go');
  const { unit, drive } = installer({ key: null, release });
  await unit.restart();
  await unit.install();
  assert.equal(unit.state.trusted, false);
  assert.deepEqual(drive.files.get(DRIVE_FILE), release.bytes);
});

test('an image of the other product is never written', async () => {
  const { unit, drive } = installer({ release: releaseUf2('go', { imageProduct: 'bravo' }) });
  await unit.restart();
  await unit.install();
  assert.equal(unit.state.notice.key, 'image_wrong_product');
  assert.equal(drive.files.has(DRIVE_FILE), false);
});

test('a write the drive refuses before the last block is said', async () => {
  const { unit } = installer({ drive: new FakeDrive(T_ECHO_INFO, { failWrite: true }) });
  await unit.restart();
  await unit.install();
  assert.deepEqual({ phase: unit.state.phase, key: unit.state.notice.key }, { phase: 'drive', key: 'write_failed' });
});

// Chrome cut the write itself on a T-Echo, once the bootloader had flashed the image and restarted
test('a restart that cuts the last write or close still leaves the build ready to verify', async () => {
  for (const restartIn of ['write', 'close']) {
    const { unit, drive, recorded } = installer({ drive: new FakeDrive(T_ECHO_INFO, { restartIn }) });
    await unit.restart();
    await unit.install();
    assert.equal(drive.flashed, true);
    assert.deepEqual({ phase: unit.state.phase, notice: unit.state.notice }, { phase: 'written', notice: null }, restartIn);
    assert.match(recorded.find(([name]) => name === 'installer.wrote')[1].detail, /state had changed/);
  }
});
