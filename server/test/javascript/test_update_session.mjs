import test from 'node:test';
import assert from 'node:assert/strict';

import { FakeSkyblip } from './fake_skyblip.mjs';
import { joinSession } from '#skyblip/update';

// The update and settings pages each join the session as Turbo shows them, and the link outlives both.
test('a page that leaves the session hears nothing more, and the next page finds the link still up', async () => {
  const device = new FakeSkyblip({ bufSize: 300 }).install();
  const heard = { update: 0, settings: 0 };
  const update = joinSession(() => heard.update++);
  await update.updater.connect();
  update.leave();
  const before = heard.update;

  let answered;
  const answer = new Promise(resolve => (answered = resolve));
  const settings = joinSession(() => {
    heard.settings++;
    answered();
  });
  assert.equal(settings.updater, update.updater);
  assert.equal(settings.updater.state.phase, 'ready');
  assert.ok(device.connected);
  settings.updater.refresh();
  await answer;
  assert.equal(heard.update, before);

  settings.updater.disconnect();
  settings.leave();
});
