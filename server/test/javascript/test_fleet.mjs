import test from 'node:test';
import assert from 'node:assert/strict';

import { FakeSkyblip, signedImage } from './fake_skyblip.mjs';
import { Fleet, namesOf, slugOf } from '#skyblip/fleet';

function storage() {
  const items = new Map();
  return { getItem: key => items.get(key) ?? null, setItem: (key, value) => items.set(key, value), items };
}

// The browser's chooser, answering with whichever unit in range matches the filters the page asked with.
function airwaves(...fakes) {
  const devices = new Map(fakes.map(fake => [fake, fake.device()]));
  const asked = [];
  const matches = (fake, filters) => filters.some(({ name, namePrefix, services }) =>
    (name && fake.name === name) || (namePrefix && fake.name.startsWith(namePrefix)) || (services && fake.name.startsWith('skyBlip')));
  const radio = {
    asked,
    picks: [],
    requestDevice: async ({ filters }) => {
      asked.push(filters);
      const fake = radio.picks.shift() ?? fakes.find(each => matches(each, filters));
      if (!fake || !matches(fake, filters)) throw Object.assign(new Error('User cancelled the requestDevice() chooser.'), { name: 'NotFoundError' });
      return devices.get(fake);
    },
    getDevices: null,
    deviceOf: fake => devices.get(fake),
  };
  Object.defineProperty(globalThis, 'navigator', { value: { bluetooth: radio }, configurable: true, writable: true });
  return radio;
}

const settled = (fleet, predicate) => new Promise(resolve => {
  if (predicate(fleet)) return resolve(fleet);
  const stop = fleet.listen(() => {
    if (predicate(fleet)) {
      stop();
      resolve(fleet);
    }
  });
});

test('a device is named by its product and its address, and a Go+ is the same unit as a Go', () => {
  assert.equal(slugOf('skyBlip Go 00012A'), 'go-00012a');
  assert.equal(slugOf('skyBlip Go+ 00012A'), 'go-00012a');
  assert.equal(slugOf('skyBlip Bravo 3F2A'), 'bravo-3f2a');
  assert.equal(slugOf('activate 3F2A skyBlip'), 'activate-3f2a');
  assert.equal(slugOf('SoftRF-1A2B3C'), null);
  assert.deepEqual(namesOf('go-00012a'), ['skyBlip Go 00012A', 'skyBlip Go+ 00012A']);
  assert.deepEqual(namesOf('bravo-3f2a'), ['skyBlip Bravo 3F2A']);
});

// A pilot who comes back months later finds the unit, its callsign and its firmware, before switching it on.
test('a connected device is remembered by this browser, and listed again after a reload', async () => {
  const shelf = storage();
  airwaves(new FakeSkyblip({ name: 'skyBlip Go 00012A', bufSize: 300, stored: { aircraft_type: 3, callsign: 'F-JXYZ' } }));
  const fleet = new Fleet({ storage: shelf });
  const unit = await fleet.connectNew();
  await settled(fleet, () => unit.state.running && unit.state.settings);
  unit.disconnect();

  const reloaded = new Fleet({ storage: shelf });
  const [again] = reloaded.remembered;
  assert.equal(again.slug, 'go-00012a');
  assert.equal(again.linked, false);
  assert.equal(again.record.settings.callsign, 'F-JXYZ');
  assert.equal(again.record.settings.aircraft_type, 3);
  assert.equal(again.record.running, '0.1.0+12');
  assert.equal(again.record.track, 'release');
  assert.ok(again.record.seen);
});

test('a Go and a Bravo stay connected side by side', async () => {
  const go = new FakeSkyblip({ name: 'skyBlip Go 00012A', bufSize: 300 });
  const bravo = new FakeSkyblip({ name: 'skyBlip Bravo 3F2A', bufSize: 300, product: 'skyblip_bravo', address: null });
  const radio = airwaves(go, bravo);
  const fleet = new Fleet({ storage: storage() });
  radio.picks.push(go);
  await fleet.connectNew();
  radio.picks.push(bravo);
  await fleet.connectNew();
  await settled(fleet, () => fleet.units.every(unit => unit.state?.running));
  assert.deepEqual(fleet.units.map(unit => [unit.slug, unit.linked, unit.product]), [
    ['go-00012a', true, 'skyblip_go'],
    ['bravo-3f2a', true, 'skyblip_bravo'],
  ]);
  assert.ok(go.connected && bravo.connected);
  fleet.units.forEach(unit => unit.disconnect());
});

test('a device remembered from another visit is asked for by its name only', async () => {
  const shelf = storage();
  shelf.setItem('skyblip.devices.v1', JSON.stringify([{ slug: 'go-00012a', name: 'skyBlip Go 00012A' }]));
  const radio = airwaves(new FakeSkyblip({ name: 'skyBlip Go 00012A', bufSize: 300 }));
  const fleet = new Fleet({ storage: shelf });
  const unit = await fleet.connect('go-00012a');
  assert.deepEqual(radio.asked, [[{ name: 'skyBlip Go 00012A' }, { name: 'skyBlip Go+ 00012A' }]]);
  assert.equal(unit.linked, true);
  unit.disconnect();
});

test('a device connected once in this session connects again without the chooser', async () => {
  const radio = airwaves(new FakeSkyblip({ name: 'skyBlip Go 00012A', bufSize: 300 }));
  const fleet = new Fleet({ storage: storage() });
  const unit = await fleet.connectNew();
  unit.disconnect();
  await settled(fleet, () => !unit.linked);
  await fleet.connect('go-00012a');
  assert.equal(radio.asked.length, 1);
  assert.equal(unit.linked, true);
  unit.disconnect();
});

test('a chooser closed without a pick leaves nothing new behind', async () => {
  airwaves();
  const fleet = new Fleet({ storage: storage() });
  assert.equal(await fleet.connectNew(), null);
  assert.deepEqual(fleet.units, []);
});

// Activation renames the unit live, so the record must follow it rather than leave a second row.
test('a unit activated while connected is remembered under its new name, once', async () => {
  const shelf = storage();
  const fake = new FakeSkyblip({ name: 'activate 3F2A skyBlip', bufSize: 300, address: null });
  airwaves(fake);
  const fleet = new Fleet({ storage: shelf });
  const unit = await fleet.connectNew({ activation: true });
  await settled(fleet, () => unit.state.settings);
  assert.equal(unit.needsActivation, true);
  assert.deepEqual(fleet.remembered, []);

  fake.address = 0x00012a;
  fake.answer({ command: 'get' });
  await settled(fleet, () => unit.slug === 'go-00012a');
  assert.equal(unit.needsActivation, false);
  assert.equal(unit.name, 'skyBlip Go 00012A');
  assert.deepEqual(JSON.parse(shelf.getItem('skyblip.devices.v1')).map(record => record.slug), ['go-00012a']);
  unit.disconnect();
});

test('forgetting a device drops it from this browser and from its link', async () => {
  const shelf = storage();
  const fake = new FakeSkyblip({ name: 'skyBlip Go 00012A', bufSize: 300 });
  airwaves(fake);
  const fleet = new Fleet({ storage: shelf });
  await fleet.connectNew();
  fleet.forget('go-00012a');
  assert.equal(fake.connected, false);
  assert.deepEqual(fleet.units, []);
  assert.deepEqual(JSON.parse(shelf.getItem('skyblip.devices.v1')), []);
});

test('a browser that grants devices across visits connects the remembered ones by itself', async () => {
  const shelf = storage();
  shelf.setItem('skyblip.devices.v1', JSON.stringify([{ slug: 'go-00012a', name: 'skyBlip Go 00012A' }]));
  const fake = new FakeSkyblip({ name: 'skyBlip Go 00012A', bufSize: 300 });
  const radio = airwaves(fake);
  radio.getDevices = async () => [radio.deviceOf(fake)];
  const fleet = new Fleet({ storage: shelf });
  await fleet.restore();
  assert.equal(fleet.unit('go-00012a').linked, true);
  assert.deepEqual(radio.asked, []);
  fleet.unit('go-00012a').disconnect();
});

// A pilot who picks the connected Go again in the chooser must not add a second listener to its link.
test('picking a connected device again leaves one client on its link, and an install still goes through', async () => {
  const fake = new FakeSkyblip({ name: 'skyBlip Go 00012A', bufSize: 300 });
  airwaves(fake);
  const fleet = new Fleet({ storage: storage() });
  const unit = await fleet.connectNew();
  await settled(fleet, () => unit.state.running);
  await fleet.connectNew();
  await unit.updater.connect();
  await unit.updater.choose(signedImage({ major: 0, minor: 2, revision: 0, build: 15 }, 6000), 'skyblip-go-0.2.0+15.signed.bin');
  unit.updater.install();
  await settled(fleet, () => unit.state.phase === 'confirming');
  fake.press();
  await settled(fleet, () => unit.state.phase === 'installing' || unit.state.phase === 'rebooting');
  assert.equal(unit.state.notice, null);
  unit.disconnect();
});
