import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { DRIVE_FILE, Installer, USB_VENDORS, browserSupport } from '#skyblip/installer';
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
  constructor(node) {
    this.node = node;
    this.opened = null;
    this.closed = false;
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
  constructor(info, { failWrite = false, failClose = false } = {}) {
    this.files = new Map(info === null ? [] : [['INFO_UF2.TXT', new TextEncoder().encode(info)]]);
    Object.assign(this, { failWrite, failClose });
  }

  async getFileHandle(name, { create = false } = {}) {
    if (!this.files.has(name) && !create) throw namedError('NotFoundError');
    const drive = this;
    return {
      getFile: async () => new Blob([drive.files.get(name)]),
      createWritable: async () => ({
        write: async bytes => {
          if (drive.failWrite) throw namedError('NoModificationAllowedError', 'The drive is read-only.');
          drive.files.set(name, bytes);
        },
        close: async () => {
          if (drive.failClose) throw namedError('NotFoundError', 'The device went away.');
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
  const unit = new Installer({ onChange: state => states.push(state), serial, pickDrive, download, browserKey: async () => key, identifyWithinMs: 20 });
  return { unit, node, drive, port: fakePort, asked, states };
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
    { phase: 'installed', board: 'nRF52840-TEcho-v1', version: '0.1.0+1858', trusted: true });
  const blocks = written(drive);
  const page = recordPage(blocks, LAYOUTS.go);
  assert.deepEqual(readSpan(blocks, page, RECORD_BYTES), trustRecord(KEY));
});

test('an X1 gets the Bravo image, written at its own slot0', async () => {
  const { unit, drive } = installer({ node: new FakeNode({ hwModel: 128 }), drive: new FakeDrive(X1_INFO) });
  await unit.restart();
  await unit.install();
  assert.equal(unit.state.product, 'bravo');
  assert.equal(unit.state.phase, 'installed');
  assert.ok(readSpan(written(drive), LAYOUTS.bravo.slot0, 4).every((byte, at) => byte === [0x3d, 0xb8, 0xf3, 0x96][at]));
});

test('a port that is not Meshtastic leads to the drive by hand, without a tag', async () => {
  const { unit } = installer({ node: new FakeNode({ answers: false }) });
  await unit.restart();
  assert.equal(unit.state.phase, 'drive');
  assert.deepEqual(unit.state.notice, { key: 'no_meshtastic' });
  assert.equal(unit.state.tag, null);
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
  unit.skipToDrive();
  await unit.install();
  assert.deepEqual({ phase: unit.state.phase, notice: unit.state.notice }, { phase: 'drive', notice: { key: 'not_drive', detail: null } });
  assert.equal(drive.files.has(DRIVE_FILE), false);
});

test('a UF2 drive of another board is refused, and named', async () => {
  const { unit } = installer({ drive: new FakeDrive('Board-ID: nRF52840-Feather-v1\n') });
  unit.skipToDrive();
  await unit.install();
  assert.deepEqual(unit.state.notice, { key: 'other_board', detail: 'nRF52840-Feather-v1' });
});

test('closing the folder chooser says nothing, and a refused permission says why', async () => {
  const closed = installer({ drive: namedError('AbortError', 'The user aborted a request.') });
  closed.unit.skipToDrive();
  await closed.unit.install();
  assert.equal(closed.unit.state.notice, null);

  const denied = installer({ drive: namedError('NotAllowedError', 'Permission denied.') });
  denied.unit.skipToDrive();
  await denied.unit.install();
  assert.equal(denied.unit.state.notice.key, 'drive_denied');
});

test('a browser with no key writes the release as published, and the unit will ask for its presses', async () => {
  const release = releaseUf2('go');
  const { unit, drive } = installer({ key: null, release });
  unit.skipToDrive();
  await unit.install();
  assert.equal(unit.state.trusted, false);
  assert.deepEqual(drive.files.get(DRIVE_FILE), release.bytes);
});

test('an image of the other product is never written', async () => {
  const { unit, drive } = installer({ release: releaseUf2('go', { imageProduct: 'bravo' }) });
  unit.skipToDrive();
  await unit.install();
  assert.equal(unit.state.notice.key, 'image_wrong_product');
  assert.equal(drive.files.has(DRIVE_FILE), false);
});

test('a write the drive refuses is said, and a close the restart cuts is not', async () => {
  const refused = installer({ drive: new FakeDrive(T_ECHO_INFO, { failWrite: true }) });
  refused.unit.skipToDrive();
  await refused.unit.install();
  assert.deepEqual({ phase: refused.unit.state.phase, key: refused.unit.state.notice.key }, { phase: 'drive', key: 'write_failed' });

  const restarted = installer({ drive: new FakeDrive(T_ECHO_INFO, { failClose: true }) });
  restarted.unit.skipToDrive();
  await restarted.unit.install();
  assert.equal(restarted.unit.state.phase, 'installed');
});

test('installing needs Web Serial and the folder chooser, which only Chrome and Edge on a computer have', () => {
  assert.equal(browserSupport({ navigator: { serial: {} }, showDirectoryPicker: () => {} }), 'usb');
  assert.equal(browserSupport({ navigator: { userAgentData: { mobile: true } } }), 'phone');
  assert.equal(browserSupport({ navigator: { userAgent: 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X)' } }), 'phone');
  assert.equal(browserSupport({ navigator: { userAgent: 'Mozilla/5.0 (X11; Linux x86_64; rv:140.0) Gecko/20100101 Firefox/140.0' } }), 'other');
});
