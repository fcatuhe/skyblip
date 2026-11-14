import test from 'node:test';
import assert from 'node:assert/strict';

import { LINK_REGISTRY } from '#skyblip/link_registry';
import { PROTOCOL_NAMES, protocolsOf } from '#skyblip/update';
import { connected } from './update_session.mjs';

const GO = { adsl_version: '0.1.0', alptas_version: '0.2.0' };
// INFO: fc 14nov26 every frame of this unit fits 20 bytes, so a notice on connect could only come from protocols
const NARROWEST_UNIT = { address: null, stored: { aircraft_type: 1, alarm_enabled: true, alarm_volume: 'loud', units: 'nautical', callsign: '', own_icao: 0 }, imu: 'none', knowsBrowsers: false };

test('connecting asks the radio protocols, and reads each stack the unit names', async () => {
  const { device, until } = await connected({ protocols: GO });
  const state = await until(state => state.protocols);
  assert.ok(device.commands.includes('protocols'));
  assert.deepEqual(state.protocols, [{ name: 'ADS-L', version: '0.1.0' }, { name: 'ALP-TAS', version: '0.2.0' }]);
});

test('a Bravo names its ADS-B stack', async () => {
  const { until } = await connected({ product: 'skyblip_bravo', protocols: { adsb_version: '0.1.0' } });
  const state = await until(state => state.protocols);
  assert.deepEqual(state.protocols, [{ name: 'ADS-B', version: '0.1.0' }]);
});

test('an answer naming no stack names none, and a version that is not text is passed over', () => {
  assert.deepEqual(protocolsOf({ command: 'protocols' }), []);
  assert.deepEqual(protocolsOf({ command: 'protocols', adsl_version: 1, alptas_version: '0.2.0' }), [{ name: 'ALP-TAS', version: '0.2.0' }]);
});

test('the stacks the page names are the registry\'s', () => {
  const frame = LINK_REGISTRY.frames.find(each => each.command === 'protocols');
  const stems = frame.keys.map(key => key.name).filter(name => name.endsWith('_version')).map(name => name.replace(/_version$/, ''));
  assert.deepEqual(stems, Object.keys(PROTOCOL_NAMES));
});

test('firmware from before the frame answers unknown_command, and the page shows nothing and says nothing', async () => {
  const { device, updater, until } = await connected();
  await until(state => state.settings && state.defaults);
  assert.ok(device.commands.includes('protocols'));
  assert.equal(updater.state.protocols, null);
  assert.equal(updater.state.notice, null);
});

for (const mtu of [23, 104]) {
  test(`at MTU ${mtu} a Go names its protocols and connecting raises no notice`, async () => {
    const { device, updater, until } = await connected({ ...NARROWEST_UNIT, mtu, protocols: GO });
    await until(state => state.protocols && state.settings && state.defaults);
    assert.equal(updater.state.notice, null);
    assert.equal(device.smallMtuRefusals, 0);
  });
}

test('the protocols go with the link that named them', async () => {
  const { updater, until } = await connected({ protocols: GO });
  await until(state => state.protocols);
  updater.disconnect();
  const offline = await until(state => state.phase === 'offline');
  assert.equal(offline.protocols, null);
});
