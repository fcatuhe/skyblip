import test from 'node:test';
import assert from 'node:assert/strict';

import { pairTango, tagText, tangoView } from '#skyblip/tango';

class Fleet {
  listeners = new Set();
  listen(listener) {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }
  changed() {
    for (const listener of [...this.listeners]) listener();
  }
}

function unit(fleet, { product, tag, linked = true, remembered = true, pairedTag = null, flight = 'ground', trust = 'trusted', answers = {} }) {
  const self = {
    product, tag, linked, remembered, needsActivation: false,
    state: linked ? { phase: 'ready', task: null, settings: {}, pairedTag, status: { flight }, trust, notice: null } : null,
    asked: [],
    set(patch) {
      self.state = { ...self.state, ...patch };
      fleet.changed();
    },
  };
  self.updater = {
    pair(tagAsked) {
      self.asked.push(['pair', tagAsked]);
      self.set({ phase: 'asking', task: 'pair', notice: null });
      setTimeout(() => answers.pair?.(self, tagAsked), 0);
    },
    readSettings() {
      self.asked.push(['get']);
      setTimeout(() => answers.get?.(self), 0);
    },
  };
  return self;
}

const done = (self, key) => self.set({ phase: 'ready', task: null, notice: { key, during: 'pair' } });

test('a Go with a Bravo linked beside it is offered the pairing, and so is the Bravo', () => {
  const fleet = new Fleet();
  const go = unit(fleet, { product: 'go', tag: 0x1b2c });
  const bravo = unit(fleet, { product: 'bravo', tag: 0x3f2a });
  assert.deepEqual(tangoView(go, [go, bravo]), { kind: 'pair', partner: bravo, replaces: null });
  assert.deepEqual(tangoView(bravo, [go, bravo]), { kind: 'pair', partner: go, replaces: null });
});

test('a partner remembered but away is offered to connect, and none known to set one up', () => {
  const fleet = new Fleet();
  const go = unit(fleet, { product: 'go', tag: 0x1b2c });
  const bravo = unit(fleet, { product: 'bravo', tag: 0x3f2a, linked: false });
  assert.deepEqual(tangoView(go, [go, bravo]), { kind: 'connect', partner: bravo });
  assert.deepEqual(tangoView(go, [go]), { kind: 'none' });
});

test('a paired Go names its Bravo, and a Bravo linked that is not its own is offered as a replacement', () => {
  const fleet = new Fleet();
  const go = unit(fleet, { product: 'go', tag: 0x1b2c, pairedTag: 0x0a0b });
  const old = unit(fleet, { product: 'bravo', tag: 0x0a0b, linked: false });
  assert.deepEqual(tangoView(go, [go, old]), { kind: 'paired', partner: old, pairedTag: 0x0a0b });
  const other = unit(fleet, { product: 'bravo', tag: 0x3f2a });
  assert.deepEqual(tangoView(go, [go, old, other]), { kind: 'replace', partner: other, pairedTag: 0x0a0b });
});

test('a Bravo paired with another Go is still offered, and the offer says which Go it leaves', () => {
  const fleet = new Fleet();
  const go = unit(fleet, { product: 'go', tag: 0x1b2c });
  const bravo = unit(fleet, { product: 'bravo', tag: 0x3f2a, pairedTag: 0x7777 });
  assert.deepEqual(tangoView(go, [go, bravo]), { kind: 'pair', partner: bravo, replaces: 0x7777 });
});

test('nothing is offered in flight, before activation, or without a link', () => {
  const fleet = new Fleet();
  const bravo = unit(fleet, { product: 'bravo', tag: 0x3f2a });
  assert.equal(tangoView(unit(fleet, { product: 'go', tag: 1, flight: 'airborne' }), [bravo]), null);
  assert.equal(tangoView({ ...unit(fleet, { product: 'go', tag: 1 }), needsActivation: true }, [bravo]), null);
  assert.equal(tangoView(unit(fleet, { product: 'go', tag: 1, linked: false }), [bravo]), null);
});

test('two trusted units pair with no press: Bravo is armed first, then the Go asks and Bravo reads back its Go', async () => {
  const fleet = new Fleet();
  const steps = [];
  const bravo = unit(fleet, { product: 'bravo', tag: 0x3f2a, answers: {
    pair: self => done(self, 'pair_armed'),
    get: self => self.set({ pairedTag: 0x1b2c }),
  } });
  const go = unit(fleet, { product: 'go', tag: 0x1b2c, answers: { pair: self => setTimeout(() => done(self, 'paired'), 5) } });
  await pairTango({ fleet, go, bravo, onStep: step => steps.at(-1) !== step && steps.push(step) });
  assert.deepEqual(bravo.asked, [['pair', 0x1b2c], ['get']]);
  assert.deepEqual(go.asked, [['pair', 0x3f2a]]);
  assert.deepEqual(steps, ['arming', 'pairing', 'paired']);
  assert.equal(bravo.state.pairedTag, 0x1b2c);
});

test('a Bravo that does not trust this browser is not asked, and waits for its press, and a Go that does not asks on its glass', async () => {
  const fleet = new Fleet();
  const steps = [];
  const bravo = unit(fleet, { product: 'bravo', tag: 0x3f2a, trust: 'proven', answers: { get: self => self.set({ pairedTag: 0x1b2c }) } });
  const go = unit(fleet, { product: 'go', tag: 0x1b2c, answers: { pair: self => {
    self.set({ phase: 'confirming' });
    setTimeout(() => self.set({ phase: 'asking' }), 5);
    setTimeout(() => done(self, 'paired'), 10);
  } } });
  await pairTango({ fleet, go, bravo, onStep: step => steps.at(-1) !== step && steps.push(step) });
  assert.deepEqual(steps, ['press_bravo', 'confirm_go', 'press_bravo', 'paired']);
  assert.deepEqual(bravo.asked, [['get']]);
});

test('a refusal on either side ends the pairing and says which side refused', async () => {
  const fleet = new Fleet();
  const bravo = unit(fleet, { product: 'bravo', tag: 0x3f2a, answers: { pair: self => done(self, 'in_flight') } });
  const go = unit(fleet, { product: 'go', tag: 0x1b2c });
  await assert.rejects(pairTango({ fleet, go, bravo }), { side: 'bravo', key: 'in_flight' });
  assert.deepEqual(go.asked, []);

  const armed = unit(fleet, { product: 'bravo', tag: 0x3f2a, answers: { pair: self => done(self, 'pair_armed') } });
  const lost = unit(fleet, { product: 'go', tag: 0x1b2c, answers: { pair: self => done(self, 'not_found') } });
  await assert.rejects(pairTango({ fleet, go: lost, bravo: armed }), { side: 'go', key: 'not_found' });
});

test('a Go that never gives its verdict times out rather than holding the page', async () => {
  const fleet = new Fleet();
  const bravo = unit(fleet, { product: 'bravo', tag: 0x3f2a, answers: { pair: self => done(self, 'pair_armed') } });
  const go = unit(fleet, { product: 'go', tag: 0x1b2c });
  await assert.rejects(pairTango({ fleet, go, bravo, withinMs: 20 }), { side: 'page', key: 'timeout' });
});

test('a tag is written as the four hex digits the unit advertises', () => {
  assert.equal(tagText(0x3f2a), '3F2A');
  assert.equal(tagText(0x00b1), '00B1');
  assert.equal(tagText(null), null);
});
