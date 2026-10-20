import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { LINK_REGISTRY } from '#skyblip/link_registry';

const read = path => readFileSync(new URL(path, import.meta.url), 'utf8');

const PAGES = ['../../content/pages/manage.html.erb', '../../content/pages/manage.fr.html.erb'];
const CONFIG = '../../../firmware/core/comms/config.cpp';

const PAGE_NOTICES = [
  'claimed', 'window_closed', 'older', 'wrong_key', 'too_large', 'not_image', 'flash', 'smp', 'failed', 'refused',
  'timeout', 'link_lost', 'not_skyblip', 'no_bluetooth', 'no_file', 'no_params', 'saved', 'shelf_failed', 'needs_full',
];
const DEVICE_NOTES = [
  'probation', 'reverted', 'image_refused', 'settings_prior', 'settings_defaults', 'swap_unpowered', 'went_dark_flat',
  'imu_writing', 'no_key', 'not_activated',
];
const NEVER_ASKED_BY_THE_PAGE = new Set(['no_command', 'no_version']);

const ACTIVATION_PAGES = ['../../content/pages/activate.html.erb', '../../content/pages/activate.fr.html.erb'];
const IDENTITY = '../../../firmware/core/comms/identity_link.cpp';
// INFO: fc 18oct26 what the prompt machinery and the claim answer any command, activation included
const PROMPT_REFUSALS = ['expired', 'cancelled', 'claimed', 'unknown_command'];
const SERVER_REFUSALS = [
  'invalid', 'unknown_server_key', 'expired', 'bad_signature', 'other_key', 'revoked_key', 'daily_cap', 'no_serial', 'rate_limited', 'unreachable', 'failed',
].map(key => `server_${key}`);
const ACTIVATION_NOTICES = ['activated', 'no_bluetooth', 'not_skyblip', 'link_lost', 'failed', 'refused'];

const words = (path, kind = 'manage') => new Set([...read(path).matchAll(new RegExp(`data-${kind}-word="(\\w+)"`, 'g'))].map(match => match[1]));

const pascal = name => name.replace(/(?:^|_)(\w)/g, (_, letter) => letter.toUpperCase());
const REASONS = new Map(LINK_REGISTRY.enums.reason.values.map(({ name }) => [pascal(name), name]));

function reasonsIn(path, call) {
  const source = read(path).replace(/\/\/.*$/gm, '');
  const enumerators = [...source.matchAll(new RegExp(`${call}link::Reason::(\\w+)\\)`, 'g'))].map(match => match[1]);
  return [...new Set(enumerators)].map(enumerator => {
    assert.ok(REASONS.has(enumerator), `${path} refuses with link::Reason::${enumerator}, which the registry does not name`);
    return REASONS.get(enumerator);
  });
}

function identityRefusals() {
  return reasonsIn(IDENTITY, 'refuse\\(');
}

function firmwareRefusals() {
  return reasonsIn(CONFIG, 'acknowledge\\(false, ').filter(reason => !NEVER_ASKED_BY_THE_PAGE.has(reason));
}

test('the firmware still refuses with the reasons this test reads', () => {
  assert.ok(firmwareRefusals().includes('not_approved'), `${CONFIG} no longer reads as it did`);
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
  const [english, french] = PAGES.map(page => words(page));
  assert.deepEqual([...english].sort(), [...french].sort());
});

const registryNames = enumeration => LINK_REGISTRY.enums[enumeration].values.map(({ name }) => name);
const offered = (page, list) => [...read(page).match(new RegExp(`${list}: \\{([^}]*)\\}`))[1].matchAll(/(\w+): "/g)].map(match => match[1]);

for (const page of PAGES) {
  test(`${page.split('/').pop()} offers the units and the alarm volumes under the registry's names`, () => {
    assert.deepEqual(offered(page, 'unit_names'), registryNames('units'));
    assert.deepEqual(offered(page, 'volume_names'), registryNames('alarm_volume'));
  });
}

test('the firmware still refuses activation with the reasons this test reads', () => {
  assert.ok(identityRefusals().includes('small_mtu'), `${IDENTITY} no longer reads as it did`);
});

for (const page of ACTIVATION_PAGES) {
  test(`every refusal and notice of an activation has words on ${page.split('/').pop()}`, () => {
    const keys = [...identityRefusals(), ...PROMPT_REFUSALS, ...SERVER_REFUSALS, ...ACTIVATION_NOTICES];
    const missing = keys.filter(key => !words(page, 'activate').has(key));
    assert.deepEqual(missing, []);
  });
}

test('both activation pages word the same keys', () => {
  const [english, french] = ACTIVATION_PAGES.map(page => words(page, 'activate'));
  assert.deepEqual([...english].sort(), [...french].sort());
});
