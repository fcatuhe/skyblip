import test from 'node:test';
import assert from 'node:assert/strict';

import { Replies } from '#skyblip/replies';

// A config write waits behind every SMP write of an upload, so the answer's clock starts once the write is out.
test('the wait for an answer starts once the write is out, not while it queues', async t => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const written = Promise.withResolvers();
  const replies = new Replies({ send: () => written.promise, withinMs: 5_000 });
  const answered = replies.ask({ command: 'attest' }, reply => reply.command === 'attestation');

  t.mock.timers.tick(60_000);
  written.resolve();
  await Promise.resolve();
  await Promise.resolve();
  t.mock.timers.tick(4_000);

  assert.equal(replies.take({ command: 'attestation' }), true);
  assert.deepEqual(await answered, { command: 'attestation' });
});

test('an answer that does not come within its time leaves its waiter, so a later reply goes elsewhere', async t => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const replies = new Replies({ send: async () => {}, withinMs: 5_000 });
  const answered = replies.ask({ command: 'attest' }, reply => reply.command === 'attestation');
  await Promise.resolve();
  await Promise.resolve();

  t.mock.timers.tick(5_000);

  assert.equal(await answered, null);
  assert.equal(replies.take({ command: 'attestation' }), false);
});
