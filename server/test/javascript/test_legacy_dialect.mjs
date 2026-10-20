import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import TABLE from '#skyblip/legacy_dialect_table';
import { Dialect, LINK, current, currentSimulator, legacy } from '#skyblip/legacy_dialect';
import { DEFAULTS } from './fake_skyblip.mjs';
import { asking, chosen, connected, installing } from './update_session.mjs';

const SCHEMA = JSON.parse(readFileSync(new URL('../../../schemas/legacy_dialect.v1.json', import.meta.url), 'utf8'));

test('the table the pages load is schemas/legacy_dialect.v1.json, as bin/rails legacy_dialect:generate writes it', () => {
  assert.deepEqual(TABLE, SCHEMA);
});

test('every legacy frame of the link reads in today\'s words and writes back to itself', () => {
  const commands = LINK.values.command;
  for (const entry of LINK.frames) {
    for (const selected of entry.when) {
      const frame = { cmd: Object.keys(commands).find(old => commands[old] === selected) ?? selected };
      for (const [key, held] of Object.entries(entry.where ?? {})) frame[key] = held[0];
      for (const old of Object.keys(entry.keys ?? {})) frame[old] = 1;
      for (const [key, spellings] of Object.entries(entry.values ?? {})) frame[key] = Object.keys(spellings)[0];
      const read = current(LINK, frame);
      assert.equal(read.command, selected);
      assert.deepEqual(current(LINK, read), read);
      assert.deepEqual(legacy(LINK, read), frame);
    }
  }
});

test('the dump\'s valid is the battery in its power group and the fix in its gnss group', () => {
  const power = { cmd: 'diag', group: 'power', valid: true, level: 'LOW' };
  assert.deepEqual(current(LINK, power), { command: 'diagnostics', group: 'power', battery_valid: true, power_level: 'LOW' });
  assert.deepEqual(legacy(LINK, current(LINK, power)), power);
  assert.deepEqual(current(LINK, { cmd: 'diag', group: 'gnss', valid: true }), { command: 'diagnostics', group: 'gnss', valid: true });
});

test('a refusal from before #151 reads in today\'s words', () => {
  assert.deepEqual(current(LINK, { ack: false, reason: 'unknown_cmd' }), { acknowledge: false, reason: 'unknown_command' });
});

test('until the unit has spoken a frame goes in both dialects, then in the one it speaks', () => {
  const unknown = new Dialect();
  assert.deepEqual(unknown.send({ command: 'diagnostics' }), { cmd: 'diag', command: 'diagnostics' });

  const old = new Dialect();
  old.receive({ cmd: 'status', flight: 'ground' });
  assert.deepEqual(old.send({ command: 'set', alarm_enabled: false }), { cmd: 'set', alarm: false });

  const updated = new Dialect();
  updated.receive({ command: 'status', flight: 'ground' });
  assert.deepEqual(updated.send({ command: 'set', alarm_enabled: false }), { command: 'set', alarm_enabled: false });
});

test('a simulator from a release before #151 answers to today\'s names, and a current one is left as it is', () => {
  const panel = () => 4096;
  const older = currentSimulator({ fb: panel, W: 200, setSats: () => {} });
  assert.equal(older.panel, panel);
  assert.equal(older.width, 200);
  assert.equal(typeof older.setSatellites, 'function');

  const newer = { panel, width: 200 };
  assert.deepEqual(currentSimulator({ ...newer }), newer);
});

for (const legacyUnit of [true, false]) {
  const age = legacyUnit ? 'a unit from before #151' : 'a unit since #151';
  const ownWords = (command, fields = {}) =>
    (legacyUnit ? { cmd: command, ...fields } : { command, ...fields });

  test(`the update page reads ${age} in today's words`, async () => {
    const { device, updater, until } = await connected({ legacy: legacyUnit, stored: { ...DEFAULTS, alarm_enabled: false } });
    const read = await until(state => state.settings && state.defaults);
    assert.equal(read.settings.alarm_enabled, false);
    assert.equal(read.status.flight, 'ground');
    assert.deepEqual(device.heard[0], { cmd: 'update', command: 'update' });
    assert.deepEqual(device.heard.find(frame => (frame.cmd ?? frame.command) === 'defaults'), ownWords('defaults'));
    assert.equal(updater.state.notice, null);
  });

  test(`a settings change reaches ${age} in its own words`, async () => {
    const { device, updater, until } = await connected({ legacy: legacyUnit, stored: { ...DEFAULTS, alarm_enabled: false } });
    await until(state => state.settings && state.defaults);
    updater.saveSettings({ ...DEFAULTS, alarm_enabled: true });
    await until(asking('set'));
    assert.deepEqual(device.heard.at(-1), legacyUnit ? { cmd: 'set', alarm: true } : { command: 'set', alarm_enabled: true });
    device.press();
    const saved = await until(state => state.notice?.key === 'saved' && state.settings.alarm_enabled);
    assert.equal(saved.phase, 'ready');
  });

  test(`${age} takes an install over a link that never exchanged its MTU`, async () => {
    const { device, updater, until } = await chosen({ legacy: legacyUnit, bufSize: 300, mtu: 23 });
    updater.install();
    await until(asking('dfu'));
    assert.deepEqual(device.heard.at(-1), ownWords('dfu', { version: '0.2.0+15' }));
    device.press();
    await until(installing);
  });
}
