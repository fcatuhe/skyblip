import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { LINK_REGISTRY } from '#skyblip/link_registry';

const read = path => readFileSync(new URL(path, import.meta.url), 'utf8');

const PAGES = ['../../content/pages/update.html.erb', '../../content/pages/update.fr.html.erb'];
const CONFIG = '../../../firmware/core/comms/config.cpp';

const PAGE_NOTICES = [
  'claimed', 'window_closed', 'older', 'wrong_key', 'wrong_product', 'unmarked_image', 'too_large', 'not_image', 'flash', 'smp', 'failed', 'refused',
  'timeout', 'link_lost', 'not_skyblip', 'no_bluetooth', 'no_file', 'no_params', 'shelf_failed', 'needs_full',
];
const DEVICE_NOTES = [
  'probation', 'reverted', 'image_refused', 'settings_prior', 'settings_defaults', 'swap_unpowered', 'went_dark_flat',
  'imu_writing', 'no_key', 'not_activated',
];
const NEVER_ASKED_BY_THE_PAGE = new Set(['no_command', 'no_version', 'invalid']);

const CAPTURE = '../../../firmware/core/comms/capture_request.cpp';
const CAPTURE_NOTICES = ['capture_armed', 'capture_stopped'];
// INFO: fc 12nov26 the page offers the registry's profiles by name, so it never sends one past stop
const NEVER_SENT_BY_THE_PAGE = new Set([...NEVER_ASKED_BY_THE_PAGE, 'out_of_range']);
const CONFIGURE_PAGES = ['../../content/pages/configure.html.erb', '../../content/pages/configure.fr.html.erb'];
// INFO: fc 11nov26 what config.cpp answers a set with, the claim and the link included
const SET_REFUSALS = ['in_flight', 'low_power', 'expired', 'cancelled', 'prompt_pending', 'refused_on_device', 'small_mtu', 'unknown_command'];
const SETTINGS_PAGE_NOTICES = [
  'claimed', 'saved', 'smp', 'failed', 'refused', 'timeout', 'link_lost', 'not_skyblip', 'no_bluetooth',
  'settings_prior', 'settings_defaults', 'not_activated',
];

const ACTIVATION_PAGES = ['../../content/pages/activate.html.erb', '../../content/pages/activate.fr.html.erb'];
const IDENTITY = '../../../firmware/core/comms/identity_link.cpp';
// INFO: fc 18oct26 what the prompt machinery and the claim answer any command, activation included
const PROMPT_REFUSALS = ['expired', 'cancelled', 'prompt_pending', 'refused_on_device', 'claimed', 'unknown_command'];
const REPLY_REFUSALS = ['small_mtu'];
const SERVER_REFUSALS = [
  'invalid', 'unknown_server_key', 'expired', 'bad_signature', 'other_key', 'revoked_key', 'daily_cap', 'network_cap', 'no_serial', 'rate_limited', 'unreachable', 'failed',
].map(key => `server_${key}`);
const ACTIVATION_NOTICES = ['activated', 'saved', 'skipped', 'no_bluetooth', 'not_skyblip', 'link_lost', 'failed', 'refused'];
// INFO: fc 10nov26 a set's refusals whose activation words would mislead, so the page words them as the settings'
const SETTINGS_REFUSALS = ['in_flight', 'low_power', 'expired', 'cancelled', 'prompt_pending', 'refused_on_device'];

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
  assert.ok(firmwareRefusals().includes('small_mtu'), `${CONFIG} no longer refuses a reply too wide for its link`);
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

test('the firmware still refuses a set with the reasons the settings page words', () => {
  const missing = SET_REFUSALS.filter(reason => !firmwareRefusals().includes(reason));
  assert.deepEqual(missing, []);
});

for (const page of CONFIGURE_PAGES) {
  test(`every refusal and notice of a set has words on ${page.split('/').pop()}`, () => {
    const missing = [...SET_REFUSALS, ...SETTINGS_PAGE_NOTICES].filter(key => !words(page).has(key));
    assert.deepEqual(missing, []);
  });
}

test('the firmware still refuses a capture with the reasons this test reads', () => {
  assert.ok(reasonsIn(CAPTURE, 'refuse\\(').includes('not_capturing'), `${CAPTURE} no longer reads as it did`);
});

for (const page of CONFIGURE_PAGES) {
  test(`every refusal and notice of a capture has words on ${page.split('/').pop()}`, () => {
    const refusals = reasonsIn(CAPTURE, 'refuse\\(').filter(reason => !NEVER_SENT_BY_THE_PAGE.has(reason));
    const missing = [...refusals, ...PROMPT_REFUSALS, ...CAPTURE_NOTICES].filter(key => !words(page).has(key));
    assert.deepEqual(missing, []);
  });
}

test('both settings pages word the same keys', () => {
  const [english, french] = CONFIGURE_PAGES.map(page => words(page));
  assert.deepEqual([...english].sort(), [...french].sort());
});

for (const page of CONFIGURE_PAGES) {
  test(`${page.split('/').pop()} offers the units, the alarm volumes and the captures under the registry's names`, () => {
    assert.deepEqual(offered(page, 'unit_names'), registryNames('units'));
    assert.deepEqual(offered(page, 'volume_names'), registryNames('alarm_volume'));
    assert.deepEqual(offered(page, 'profile_names'), registryNames('capture_profile').filter(name => name !== 'stop'));
  });
}

test('the firmware still refuses activation with the reasons this test reads', () => {
  assert.ok(identityRefusals().includes('bad_signature'), `${IDENTITY} no longer reads as it did`);
});

for (const page of ACTIVATION_PAGES) {
  test(`every refusal and notice of an activation has words on ${page.split('/').pop()}`, () => {
    const keys = [...identityRefusals(), ...PROMPT_REFUSALS, ...REPLY_REFUSALS, ...SERVER_REFUSALS, ...ACTIVATION_NOTICES];
    const missing = keys.filter(key => !words(page, 'activate').has(key));
    assert.deepEqual(missing, []);
  });
}

test('the firmware still refuses a set with the reasons the activation page words for the settings', () => {
  const missing = SETTINGS_REFUSALS.filter(reason => !firmwareRefusals().includes(reason));
  assert.deepEqual(missing, []);
});

for (const page of ACTIVATION_PAGES) {
  test(`every refusal of the settings offered after an activation has words on ${page.split('/').pop()}`, () => {
    const missing = SETTINGS_REFUSALS.map(reason => `set_${reason}`).filter(key => !words(page, 'activate').has(key));
    assert.deepEqual(missing, []);
  });
}

test('both activation pages word the same keys', () => {
  const [english, french] = ACTIVATION_PAGES.map(page => words(page, 'activate'));
  assert.deepEqual([...english].sort(), [...french].sort());
});
