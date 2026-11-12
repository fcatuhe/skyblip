import test from 'node:test';
import assert from 'node:assert/strict';

import { connect } from '#skyblip/ble';
import { LogReader, fetchDiagnostics } from '#skyblip/log';
import { FakeSkyblip } from './fake_skyblip.mjs';
import { FakeLogStore } from './fake_log.mjs';

const adsb = address => {
  const raw = new Uint8Array(24);
  raw[0] = 21;
  raw.set([address & 0xff, (address >> 8) & 0xff, address >> 16], 8);
  return raw;
};

const sessionOf = (session, count, options = {}) => {
  const records = new Uint8Array(count * 24);
  for (let at = 0; at < count; at++) records.set(adsb(0x3c0000 + at), at * 24);
  return { session, records, ...options };
};

async function bravoWith(store, { timeoutMs } = {}) {
  const fake = new FakeSkyblip({ product: 'skyblip_bravo', name: 'skyBlip Bravo 3F2A', logStore: store }).install();
  let reader = null;
  const link = await connect({ onReply: () => {}, onLog: (...reply) => reader.receive(...reply), onClose: () => {} });
  reader = new LogReader(fields => link.sendLog(fields), { timeoutMs });
  return { fake, link, reader };
}

test('the last session arrives whole across read windows, in the lines blip.py writes', async () => {
  const store = new FakeLogStore({ sessions: [sessionOf(100, 3), sessionOf(200, 60, { closed: true })] });
  const { link, reader } = await bravoWith(store);
  const progress = [];
  const { session, lines } = await fetchDiagnostics(reader, { onProgress: (at, total) => progress.push([at, total]) });
  assert.equal(session.session, 200);
  assert.deepEqual(lines[0], { type: 'session', log: 'diagnostics', session: 200, records: 60, closed: true, truncated: false });
  assert.deepEqual(lines.slice(1).map(line => [line.index, line.address]), Array.from({ length: 60 }, (_, at) => [at, 0x3c0000 + at]));
  assert.deepEqual(Object.keys(lines[1]).slice(0, 4), ['log', 'session', 'index', 'type']);
  // MTU 185 carries six diagnostics records a chunk, so eight chunks are 48 records.
  assert.deepEqual(store.reads, [{ from: 0, count: 8 }, { from: 48, count: 8 }]);
  assert.deepEqual(progress.at(-1), [60, 60]);
  link.disconnect();
});

test('a session the unit never closed drops its last record, with a note saying which', async () => {
  const { link, reader } = await bravoWith(new FakeLogStore({ sessions: [sessionOf(300, 4, { closed: false })] }));
  const { lines } = await fetchDiagnostics(reader);
  assert.deepEqual(lines.map(line => line.type), ['session', 'adsb', 'adsb', 'adsb', 'torn']);
  assert.equal(lines.at(-1).index, 3);
  link.disconnect();
});

test('a store holding no session fetches nothing', async () => {
  const { link, reader } = await bravoWith(new FakeLogStore());
  assert.equal(await fetchDiagnostics(reader), null);
  link.disconnect();
});

test('a refusal stops the fetch and names its reason', async () => {
  const { link, reader } = await bravoWith(new FakeLogStore({ refuse: 'no_diagnostics' }));
  await assert.rejects(fetchDiagnostics(reader), /refused: no_diagnostics/);
  link.disconnect();
});

test('a unit that never answers fails the fetch rather than hang it', async () => {
  const { link, reader } = await bravoWith(new FakeLogStore({ silent: true }), { timeoutMs: 1 });
  await assert.rejects(fetchDiagnostics(reader), /no reply on the log endpoint/);
  link.disconnect();
});

// The console keeps its link between fetches, so a chunk of a window a timeout abandoned can land in the next fetch.
test('a chunk left over from an earlier read is passed over, not taken for the answer', async () => {
  const stray = { command: 'chunk', log: 'diagnostics', session: 200, from: 1, ends_session: false, records: new Uint8Array(24) };
  const { link, reader } = await bravoWith(new FakeLogStore({ sessions: [sessionOf(100, 3), sessionOf(200, 2, { closed: true })], stray }));
  const { session, lines } = await fetchDiagnostics(reader);
  assert.equal(session.session, 200);
  assert.deepEqual(lines.map(line => line.type), ['session', 'adsb', 'adsb']);
  link.disconnect();
});

test('a refusal carries its reason for the page to word', async () => {
  const { link, reader } = await bravoWith(new FakeLogStore({ refuse: 'small_mtu' }));
  await assert.rejects(fetchDiagnostics(reader), error => error.reason === 'small_mtu');
  link.disconnect();
});
