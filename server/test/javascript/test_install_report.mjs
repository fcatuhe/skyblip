// What the page makes of a unit's install report (#278, retired with #294).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installCause, installReportOf, reportLine, reportShown } from '#skyblip/install_report';
import { connected } from './update_session.mjs';

const REVERTED = { state: 'reverted', from: '0.1.0+12', to: '0.2.0+15' };
const MARKED = { check: 'matched', swap: 'test', requestError: 0, boots: 1, probationBoots: 0, proof: 'none', reset: 'software' };

test('a bare install_report frame is no report, and a full one reads under the page names', () => {
  assert.equal(installReportOf({ command: 'install_report' }), null);
  const report = installReportOf({
    command: 'install_report', check: 'matched', swap: 'test', request_error: -5, boots: 2, probation_boots: 1, proof: 'started', reset: 'watchdog',
  });
  assert.deepEqual(report, { check: 'matched', swap: 'test', requestError: -5, boots: 2, probationBoots: 1, proof: 'started', reset: 'watchdog' });
});

test('the report names the step that failed, from slot 1 to the probation', () => {
  const cause = report => installCause({ image: REVERTED, report: { ...MARKED, ...report } });
  assert.equal(cause({ check: 'damaged', swap: 'none' }), 'slot_damaged');
  assert.equal(cause({ check: 'unreadable', swap: 'none' }), 'slot_unreadable');
  assert.equal(cause({ check: 'undigested', swap: 'none' }), 'slot_undigested');
  assert.equal(cause({ requestError: -14 }), 'swap_not_marked');
  assert.equal(cause({ swap: 'none' }), 'swap_not_marked');
  assert.equal(cause({}), 'bootloader_refused');
  assert.equal(cause({ boots: 2, probationBoots: 1, proof: 'started', reset: 'watchdog' }), 'probation_failed');
});

test('an image still on probation has no cause yet, and a landed install none at all', () => {
  assert.equal(installCause({ image: { state: 'probation' }, report: { ...MARKED, probationBoots: 1 } }), null);
  assert.equal(installCause({ image: { state: 'confirmed' }, report: null, running: '0.2.0+15', attempted: '0.2.0+15' }), null);
  assert.equal(reportShown({ image: { state: 'confirmed' }, cause: null }), false);
});

// The silence of 12 Nov: back on the old version, confirmed, and nothing on record.
test('a unit back on its old version with no record says so, and shows the report line', () => {
  const cause = installCause({ image: { state: 'confirmed' }, report: null, running: '0.1.0+12', attempted: '0.2.0+15' });
  assert.equal(cause, 'no_record');
  assert.equal(reportShown({ image: { state: 'confirmed' }, cause }), true);
  assert.equal(reportShown({ image: REVERTED, cause: null }), true, 'firmware older than the report still gets a line');
});

test('the report line carries every field in one copyable line, a dash for what is unknown', () => {
  const line = reportLine({
    product: 'skyblip_bravo', running: '0.1.0+12', attempted: '0.2.0+15', image: REVERTED,
    report: { ...MARKED, boots: 2, probationBoots: 1, proof: 'gnss_heard', reset: 'watchdog' },
    cause: 'probation_failed', at: '2026-11-13T15:22:04.000Z', browser: 'Mozilla/5.0 (Linux; Android 15) Chrome/131',
  });
  assert.equal(line, [
    'skyblip-install-report v1 product=skyblip_bravo running=0.1.0+12 tried=0.2.0+15 image=reverted from=0.1.0+12 to=0.2.0+15',
    'cause=probation_failed check=matched swap=test request_error=0 boots=2 probation_boots=1 proof=gnss_heard reset=watchdog',
    'at=2026-11-13T15:22:04.000Z browser="Mozilla/5.0 (Linux; Android 15) Chrome/131"',
  ].join(' '));
  assert.equal(line.includes('\n'), false);

  const bare = reportLine({ product: 'skyblip_go', running: '0.1.0+12', image: { state: 'confirmed' }, report: null, cause: 'no_record' });
  assert.match(bare, / tried=- image=confirmed from=- to=- cause=no_record report=none at=- browser=""$/);
});

test('the page asks for the report on connecting, and reads it', async () => {
  const report = { check: 'matched', swap: 'test', request_error: 0, boots: 2, probation_boots: 1, proof: 'started', reset: 'watchdog' };
  const { device, until } = await connected({ image: 'reverted', from: '0.1.0+12', to: '0.2.0+15', installReport: report });
  const state = await until(state => state.installReport);
  assert.equal(state.installReport.probationBoots, 1);
  assert.ok(device.commands.includes('install_report'));
});

test('firmware older than the report answers unknown_command, and the page says nothing of it', async () => {
  const { device, updater } = await connected({ image: 'reverted', from: '0.1.0+12', to: '0.2.0+15' });
  assert.ok(device.commands.includes('install_report'));
  assert.equal(updater.state.installReport, null);
  assert.equal(updater.state.notice, null);
  assert.equal(updater.state.phase, 'ready');
});
