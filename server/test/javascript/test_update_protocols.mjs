import test from 'node:test';
import assert from 'node:assert/strict';

import { connected } from './update_session.mjs';

const GO = {
  adsl_version: '0.1.0', adsl_specification: 'ADS-L 4 SRD-860 Issue 2',
  alptas_version: '0.2.0', alptas_specification: 'ALP-TAS 2024 FLARM-generation frame',
};

test('connecting asks the radio protocols, and reads each stack the unit names', async () => {
  const { device, until } = await connected({ protocols: GO });
  const state = await until(state => state.protocols);
  assert.ok(device.commands.includes('protocols'));
  assert.deepEqual(state.protocols, [
    { name: 'ADS-L', version: '0.1.0', specification: 'ADS-L 4 SRD-860 Issue 2' },
    { name: 'ALP-TAS', version: '0.2.0', specification: 'ALP-TAS 2024 FLARM-generation frame' },
  ]);
});

test('a Bravo names its ADS-B stack', async () => {
  const bravo = { adsb_version: '0.1.0', adsb_specification: '1090 MHz extended squitter DF17' };
  const { until } = await connected({ product: 'skyblip_bravo', protocols: bravo });
  const state = await until(state => state.protocols);
  assert.deepEqual(state.protocols, [{ name: 'ADS-B', version: '0.1.0', specification: '1090 MHz extended squitter DF17' }]);
});

test('firmware from before the frame answers unknown_command, and the page shows nothing and says nothing', async () => {
  const { device, updater, until } = await connected();
  await until(state => state.settings && state.defaults);
  assert.ok(device.commands.includes('protocols'));
  assert.equal(updater.state.protocols, null);
  assert.equal(updater.state.notice, null);
});

test('the protocols go with the link that named them', async () => {
  const { updater, until } = await connected({ protocols: GO });
  await until(state => state.protocols);
  updater.disconnect();
  const offline = await until(state => state.phase === 'offline');
  assert.equal(offline.protocols, null);
});
