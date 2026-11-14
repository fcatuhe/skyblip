import test from 'node:test';
import assert from 'node:assert/strict';

import { Journal, browserCodeOf, browserFactsOf, httpTransport, supportCodeOf } from '#skyblip/journal';

function storage() {
  const items = new Map();
  return { getItem: key => items.get(key) ?? null, setItem: (key, value) => items.set(key, value), items };
}

function journal({ shelf = storage(), send = async () => true, ids = 0, at = '2026-11-14T10:00:00Z' } = {}) {
  let id = ids;
  const timers = [];
  const made = new Journal({
    storage: shelf, send, now: () => new Date(at), uuid: () => `00000000-0000-4000-8000-${String(++id).padStart(12, '0')}`,
    random: size => new Uint8Array(size).map((_, index) => index * 5), schedule: work => timers.push(work), path: () => '/briefing/devices/go-00012a',
  });
  return { journal: made, shelf, timers };
}

test('a browser is named by eight Crockford letters, which pilots read as two groups of four', () => {
  assert.equal(browserCodeOf(new Uint8Array([0, 1, 10, 31, 32, 33, 200, 255])), '01AZ018Z');
  assert.equal(supportCodeOf('01AZ018Z'), '01AZ-018Z');
  assert.equal(supportCodeOf(null), null);
});

// The CNIL lets a tracker live 13 months from the visit that set it, never renewed by the next.
test('the support code is minted once, read back by the next page, and minted again 13 months later', () => {
  const { journal: first, shelf } = journal();
  const code = first.browser;
  assert.match(code, /^[0-9A-HJKMNP-TV-Z]{8}$/);
  assert.equal(journal({ shelf, at: '2027-12-13T10:00:00Z' }).journal.browser, code);
  assert.equal(JSON.parse(shelf.getItem('skyblip.browser.v1')).until, '2027-12-14T10:00:00.000Z');

  let random = 1;
  const later = new Journal({ storage: shelf, send: async () => true, now: () => new Date('2027-12-14T10:00:01Z'), uuid: () => 'u', random: size => new Uint8Array(size).fill(random++) });
  assert.notEqual(later.browser, code);
});

// A pilot at the airfield has no network, and the journal waits for one rather than losing what went wrong.
test('an entry waits in the outbox until the server takes it, across a reload', async () => {
  let online = false;
  const sent = [];
  const send = async batch => {
    if (online) sent.push(batch);
    return online;
  };
  const { journal: offline, shelf, timers } = journal({ send });
  offline.record('device.noticed', { key: 'link_lost', detail: null }, { device: 'go-00012a' });
  assert.equal(await offline.flush(), false);
  assert.equal(offline.pending, 1);
  assert.equal(timers.length, 1, 'it tries again later');

  online = true;
  const { journal: reloaded } = journal({ shelf, send, ids: 10 });
  assert.equal(await reloaded.flush(), true);
  assert.equal(reloaded.pending, 0);
  const [{ browser, entries: [entry] }] = sent;
  assert.equal(browser, offline.browser);
  assert.deepEqual(entry, {
    id: '00000000-0000-4000-8000-000000000002', name: 'device.noticed', at: '2026-11-14T10:00:00.000Z', seq: 1,
    page: '00000000-0000-4000-8000-000000000001', path: '/briefing/devices/go-00012a', device: 'go-00012a', properties: { key: 'link_lost' },
  });
});

test('the outbox keeps the newest 500 entries, and sends them 50 at a time', async () => {
  const batches = [];
  const { journal: busy } = journal({ send: async batch => batches.push(batch.entries.length) });
  for (let index = 0; index < 520; index++) busy.record('browser.clicked', { control: `button ${index}` });
  assert.equal(busy.pending, 500);
  await busy.flush();
  assert.deepEqual(batches, Array(10).fill(50));
});

test('a send that throws keeps the entries for the next try', async () => {
  const { journal: failing } = journal({ send: async () => { throw new TypeError('Failed to fetch'); } });
  failing.record('browser.viewed', { navigation: 'load' });
  assert.equal(await failing.flush(), false);
  assert.equal(failing.pending, 1);
});

test('the transport posts the journal with the browser and the page token, and drops one the server will never take', async () => {
  const calls = [];
  const answer = status => async (url, options) => {
    calls.push({ url, options });
    return { ok: status < 300, status };
  };
  const batch = { browser: '01AZ018Z', entries: [] };
  assert.equal(await httpTransport({ url: '/events', token: 'csrf', fetch: answer(204) })(batch), true);
  assert.equal(await httpTransport({ url: '/events', fetch: answer(400) })(batch), true);
  assert.equal(await httpTransport({ url: '/events', fetch: answer(422) })(batch), false);
  assert.equal(await httpTransport({ url: '/events', fetch: answer(429) })(batch), false);
  const [{ url, options }] = calls;
  assert.equal(url, '/events');
  assert.equal(options.keepalive, true);
  assert.equal(options.headers['X-CSRF-Token'], 'csrf');
  assert.equal(options.headers['X-Briefing-Browser'], '01AZ018Z');
});

test('a browser is told by its family, major version and system, never its whole user agent', () => {
  const android = { userAgent: 'Mozilla/5.0 (Linux; Android 15; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Mobile Safari/537.36', language: 'fr-FR' };
  assert.deepEqual(browserFactsOf(android), { browser_name: 'Chrome', browser_major: 131, os_name: 'Android', mobile: true, language: 'fr-FR' });
  const iphone = { userAgent: 'Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1' };
  assert.deepEqual(browserFactsOf(iphone), { browser_name: 'Safari', browser_major: 17, os_name: 'iOS', mobile: true, language: null });
  const edge = { userAgent: 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36 Edg/131.0.0.0', userAgentData: { platform: 'Windows', mobile: false } };
  assert.equal(browserFactsOf(edge).browser_name, 'Edge');
  assert.equal(browserFactsOf(edge).mobile, false);
});
