import test from 'node:test';
import assert from 'node:assert/strict';
import { changesSince } from '../../app/javascript/controllers/briefing/changelog.js';

const section = (version, channel = 'release') => ({ dataset: { version, channel } });

test('the changes are those of every release past the running version, up to the offered one', () => {
  const sections = [section('0.4.0'), section('0.3.0'), section('0.2.0'), section('0.1.0')];

  const shown = changesSince(sections, { channel: 'release', running: '0.1.0+1700', offered: '0.3.0+1900' });

  assert.deepEqual(shown.map(({ dataset }) => dataset.version), ['0.3.0', '0.2.0']);
});

// A stable tag carries no build number, so the release the unit runs compares as older than its own version.
test('the release the unit already runs is not among the changes', () => {
  const shown = changesSince([section('0.2.0')], { channel: 'release', running: '0.2.0+1800', offered: '0.2.0+1800' });

  assert.deepEqual(shown, []);
});

test('only the releases of the unit\'s channel count, and an unknown version shows nothing', () => {
  const sections = [section('0.1.0+1693', 'development'), section('0.1.0+1690', 'release')];

  assert.deepEqual(changesSince(sections, { channel: 'development', running: '0.1.0+1677', offered: '0.1.0+1693' }), [sections[0]]);
  assert.deepEqual(changesSince(sections, { channel: 'development', running: null, offered: '0.1.0+1693' }), []);
});
