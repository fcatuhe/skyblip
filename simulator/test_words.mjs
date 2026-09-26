import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = path => readFileSync(new URL(path, import.meta.url), 'utf8');

const PAGES = ['../website/content/pages/manage.html.erb', '../website/content/pages/manage.fr.html.erb'];
const CONFIG = '../firmware/core/comms/config.cpp';

const PAGE_NOTICES = [
  'claimed', 'window_closed', 'not_newer', 'too_large', 'not_image', 'flash', 'smp', 'failed', 'refused',
  'timeout', 'link_lost', 'not_skyblip', 'no_bluetooth', 'no_file', 'no_params', 'upload_unfinished',
];
const DEVICE_NOTES = ['probation', 'reverted', 'settings_prior', 'settings_defaults', 'swap_unpowered', 'went_dark_flat'];
const NEVER_ASKED_BY_THE_PAGE = new Set(['no_cmd']);

const words = path => new Set([...read(path).matchAll(/data-manage-word="(\w+)"/g)].map(match => match[1]));

function firmwareRefusals() {
  const source = read(CONFIG);
  const acked = [...source.matchAll(/ack\(false, "(\w+)"\)/g)].map(match => match[1]);
  const staging = /ConfigService::staging_refusal\(\) const \{([\s\S]*?)\n\}/.exec(source);
  const returned = staging ? [...staging[1].matchAll(/return "(\w+)";/g)].map(match => match[1]) : [];
  return [...new Set([...acked, ...returned])].filter(reason => !NEVER_ASKED_BY_THE_PAGE.has(reason));
}

test('the firmware still refuses with the reasons this test reads', () => {
  assert.ok(firmwareRefusals().includes('nothing_staged'), `${CONFIG} no longer reads as it did`);
});

for (const page of PAGES) {
  test(`every refusal config.cpp can answer has words on ${page.split('/').pop()}`, () => {
    const missing = firmwareRefusals().filter(reason => !words(page).has(reason));
    assert.deepEqual(missing, []);
  });

  test(`every notice the update client raises has words on ${page.split('/').pop()}`, () => {
    const missing = [...PAGE_NOTICES, ...DEVICE_NOTES].filter(key => !words(page).has(key));
    assert.deepEqual(missing, []);
  });
}

test('both pages word the same keys', () => {
  const [english, french] = PAGES.map(words);
  assert.deepEqual([...english].sort(), [...french].sort());
});
