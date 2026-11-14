import test from 'node:test';
import assert from 'node:assert/strict';
import { gunzipSync } from 'node:zlib';

import { sendCapture, profileOf } from '#skyblip/capture_upload';
import { ndjson } from '#skyblip/diagnostics';
import { FakeLogStore } from './fake_log.mjs';
import { connected } from './update_session.mjs';

const sessionOf = (session, count) => ({ session, records: new Uint8Array(count * 24).map((_, at) => (at % 24 === 0 ? 8 : 0)) });

test('the page fetches the last capture off the log endpoint, and says how far it got', async () => {
  const { updater } = await connected({ logStore: new FakeLogStore({ sessions: [sessionOf(100, 3), sessionOf(200, 30)] }) });
  const seen = [];
  const fetching = updater.fetchCapture();
  assert.equal(updater.state.task, 'fetch');
  seen.push(updater.state.fetching);
  const fetched = await fetching;
  assert.equal(fetched.session.session, 200);
  assert.equal(fetched.lines.length, 31);
  assert.equal(updater.state.phase, 'ready');
  assert.equal(updater.state.fetching, null);
  assert.deepEqual(seen, [{ at: 0, total: null }]);
});

test('an empty diagnostics store fetches nothing, and says no failure', async () => {
  const { updater } = await connected();
  assert.equal(await updater.fetchCapture(), null);
  assert.equal(updater.state.notice, null);
});

test('a fetch the unit refuses is a notice under the capture control', async () => {
  const { updater } = await connected({ logStore: new FakeLogStore({ refuse: 'no_diagnostics' }) });
  assert.equal(await updater.fetchCapture(), null);
  assert.equal(updater.state.notice.key, 'no_diagnostics');
  assert.equal(updater.state.notice.during, 'capture');
});

// The server reads the session and the records off the file, so the gzip must be the NDJSON blip.py writes.
test('a capture is sent gzipped, with the profile its boot record names, the browser and the token', async () => {
  const lines = [
    { type: 'session', log: 'diagnostics', session: 200, records: 2, closed: true, truncated: false },
    { log: 'diagnostics', session: 200, index: 0, type: 'boot', profile: 'power_run' },
    { log: 'diagnostics', session: 200, index: 1, type: 'power', percent: 80 },
  ];
  let sent = null;
  const fetch = async (url, init) => {
    sent = { url, init };
    return new Response(JSON.stringify({ kept_until: '2028-12-15T12:00:00Z' }), { status: 201 });
  };
  const answer = await sendCapture({ url: '/briefing/devices/go-12a/captures', fetched: { session: lines[0], lines }, browser: '01AZ018Z', token: 'T', fetch });
  assert.equal(answer.kept_until, '2028-12-15T12:00:00Z');
  assert.equal(sent.init.headers['X-Briefing-Browser'], '01AZ018Z');
  assert.equal(sent.init.headers['X-CSRF-Token'], 'T');
  assert.equal(sent.init.body.get('capture[profile]'), 'power_run');
  const body = sent.init.body.get('capture[body]');
  assert.equal(body.name, 'diagnostics-200.ndjson.gz');
  assert.equal(gunzipSync(Buffer.from(await body.arrayBuffer())).toString(), ndjson(lines));
});

test('a capture with no boot record goes as full, and a refusal is an error', async () => {
  assert.equal(profileOf([{ type: 'session' }]), 'full');
  const fetch = async () => new Response('', { status: 422 });
  await assert.rejects(sendCapture({ url: '/x', fetched: { session: { session: 1 }, lines: [] }, browser: 'B', fetch }), /422/);
});
