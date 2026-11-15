import test from 'node:test';
import assert from 'node:assert/strict';
import { fitting } from '../../app/javascript/controllers/briefing/shelf.js';

const DEVELOPMENT = '212602F2';
const RELEASE = '88A4BEF5';

const option = (version, key, product = 'go') => ({ value: `/releases/${version}`, dataset: { version, key, product } });
const shelf = (...options) => ({ options: [{ value: '', dataset: {} }, ...options] });
const unit = (running, image, product = 'go') => ({ state: { running, identity: { product }, image } });
const offered = (options, of) => fitting(shelf(...options), of).map(({ dataset }) => dataset.version);

test('a release bootloader is offered its own product\'s releases, never a development build', () => {
  const options = [option('0.2.0', RELEASE), option('0.2.0+1900', DEVELOPMENT), option('0.2.0', RELEASE, 'bravo')];
  const release = unit('0.1.0+1800', { key: RELEASE, bootloaderKeys: [RELEASE] });

  assert.deepEqual(offered(options, release), ['0.2.0']);
});

test('a development unit is offered every build its bootloader verifies, older ones included', () => {
  const options = [option('0.2.0', RELEASE), option('0.2.0+1950', DEVELOPMENT), option('0.2.0+1850', DEVELOPMENT)];
  const development = unit('0.2.0+1900', { key: DEVELOPMENT, downgrade: true, bootloaderKeys: [DEVELOPMENT, RELEASE] });

  assert.deepEqual(offered(options, development), ['0.2.0', '0.2.0+1950', '0.2.0+1850']);
});

// A release refuses an older image, and a stable tag has no build number, so its own release still shows.
test('a release on a development bootloader is offered nothing older than itself', () => {
  const options = [option('0.2.0', RELEASE), option('0.1.0', RELEASE), option('0.2.0+1950', DEVELOPMENT), option('0.2.0+1850', DEVELOPMENT)];
  const release = unit('0.2.0+1900', { key: RELEASE, bootloaderKeys: [DEVELOPMENT, RELEASE] });

  assert.deepEqual(offered(options, release), ['0.2.0', '0.2.0+1950']);
});

test('firmware that names no bootloader keys is offered the images of its own key', () => {
  const options = [option('0.2.0', RELEASE), option('0.2.0+1950', DEVELOPMENT)];

  assert.deepEqual(offered(options, unit('0.1.0+1800', { key: RELEASE })), ['0.2.0']);
  assert.deepEqual(offered(options, unit('0.1.0+1800', {})), []);
});

test('a unit that names no product is offered nothing', () => {
  const options = [option('0.2.0', RELEASE)];

  assert.deepEqual(offered(options, unit('0.1.0+1800', { key: RELEASE }, null)), []);
});
