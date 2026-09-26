import test from 'node:test';
import assert from 'node:assert/strict';

import { decode, encode } from './cbor.js';
import {
  GROUP, HEADER_BYTES, ID, LARGEST_ATT_PAYLOAD, OP, SmpClient, SmpError, SmpTimeout, SmpWriteRejected,
  packet, parseHeader, probeWriteBytes, reassemblePackets, upload, uploadBody,
} from './smp.js';

const reply = (request, body) => packet({ ...parseHeader(request), op: parseHeader(request).op + 1, body });

const echoing = answer => {
  const sent = [];
  const client = new SmpClient(async bytes => {
    sent.push(bytes);
    queueMicrotask(() => client.receive(reply(bytes, answer(decode(bytes.subarray(HEADER_BYTES)), bytes))));
  });
  return { client, sent };
};

test('a request carries the SMP v2 header in network order', () => {
  const bytes = packet({ op: OP.write, group: GROUP.image, id: ID.imageUpload, seq: 42, body: {} });
  assert.deepEqual(Array.from(bytes), [0x0a, 0, 0, 1, 0, 1, 42, 1, 0xa0]);
  assert.deepEqual(parseHeader(bytes), { op: OP.write, length: 1, group: GROUP.image, seq: 42, id: ID.imageUpload });
});

// The device cuts a reply at its notification payload, so a packet can arrive in any number of pieces.
test('a reply split across notifications is one packet', () => {
  const packets = [];
  const feed = reassemblePackets(bytes => packets.push(bytes));
  const whole = packet({ op: OP.writeResponse, group: GROUP.image, id: ID.imageUpload, seq: 3, body: { off: 4096 } });
  feed(whole.slice(0, 5));
  feed(whole.slice(5, 9));
  assert.equal(packets.length, 0);
  feed(whole.slice(9));
  assert.deepEqual(packets, [whole]);
});

test('two replies in one notification are two packets', () => {
  const packets = [];
  const feed = reassemblePackets(bytes => packets.push(bytes));
  const one = packet({ op: OP.readResponse, group: GROUP.os, id: ID.osParams, seq: 0, body: { buf_size: 2475 } });
  const two = packet({ op: OP.readResponse, group: GROUP.image, id: ID.imageState, seq: 1, body: { images: [] } });
  const both = new Uint8Array([...one, ...two]);
  feed(both);
  assert.deepEqual(packets, [one, two]);
});

test('every upload packet fits the device buffer, and wastes at most a byte of it while image remains', () => {
  const image = new Uint8Array(70_000);
  const sha = new Uint8Array(32);
  for (const budget of [90, 150, 280, 281, 282, 300, 2475]) {
    for (const off of [0, 1, 23, 24, 255, 256, 65535, 65536, 69_000]) {
      const body = uploadBody(image, sha, off, budget);
      const bytes = HEADER_BYTES + encode(body).length;
      assert.ok(bytes <= budget, `budget ${budget} at ${off}: ${bytes}`);
      if (off + body.data.length < image.length) assert.ok(bytes >= budget - 1, `budget ${budget} at ${off}: ${bytes}`);
    }
  }
});

test('only the first upload packet names the length and the sha', () => {
  const image = new Uint8Array(1000);
  const sha = new Uint8Array(32).fill(1);
  assert.deepEqual(Object.keys(uploadBody(image, sha, 0, 300)), ['len', 'off', 'sha', 'data']);
  assert.deepEqual(Object.keys(uploadBody(image, sha, 300, 300)), ['off', 'data']);
});

test('the upload follows the offset the device answers, not the one it sent', async () => {
  const image = new Uint8Array(5000).map((_, at) => at & 0xff);
  const received = new Uint8Array(image.length);
  const resumeAt = 3000;
  const { client } = echoing(({ off, data }) => {
    if (off === 0) return { off: resumeAt };
    received.set(data, off);
    return { off: off + data.length };
  });
  const offsets = [];
  await upload(client, image, new Uint8Array(32), 500, sent => offsets.push(sent));
  assert.equal(offsets[0], resumeAt);
  assert.equal(offsets.at(-1), image.length);
  assert.deepEqual(received.subarray(resumeAt), image.subarray(resumeAt));
});

test('a device that stops moving the offset ends the upload', async () => {
  const { client } = echoing(() => ({ off: 0 }));
  await assert.rejects(upload(client, new Uint8Array(1000), new Uint8Array(32), 300, () => {}), /stopped at offset 0/);
});

test('a refusal in the header rc and one in the group err are both errors', async () => {
  const hook = echoing(() => ({ rc: 11 }));
  await assert.rejects(hook.client.request(OP.write, GROUP.image, ID.imageUpload, {}), error => {
    assert.ok(error instanceof SmpError);
    assert.equal(error.rc, 11);
    assert.equal(error.group, null);
    return true;
  });
  const group = echoing(() => ({ err: { group: GROUP.image, rc: 27 } }));
  await assert.rejects(group.client.request(OP.write, GROUP.image, ID.imageUpload, {}), { rc: 27, group: GROUP.image });
});

test('a legacy rc of zero is an answer, not a refusal', async () => {
  const { client } = echoing(() => ({ rc: 0, off: 12 }));
  assert.deepEqual(await client.request(OP.write, GROUP.image, ID.imageUpload, {}), { rc: 0, off: 12 });
});

test('a reply with another sequence number is not the answer', async () => {
  const sent = [];
  const client = new SmpClient(async bytes => sent.push(bytes));
  const answer = client.request(OP.read, GROUP.os, ID.osParams);
  await new Promise(setImmediate);
  const header = parseHeader(sent[0]);
  client.receive(packet({ ...header, op: OP.readResponse, seq: header.seq + 1, body: { buf_size: 1 } }));
  client.receive(packet({ ...header, op: OP.readResponse, body: { buf_size: 2475 } }));
  assert.deepEqual(await answer, { buf_size: 2475 });
});

test('requests wait their turn, one in flight at a time', async () => {
  const { client, sent } = echoing((_, bytes) => ({ seq: parseHeader(bytes).seq }));
  const answers = await Promise.all([0, 1, 2].map(() => client.request(OP.read, GROUP.os, ID.osParams)));
  assert.deepEqual(answers.map(answer => answer.seq), [0, 1, 2]);
  assert.equal(sent.length, 3);
});

test('a device that never answers times out', async t => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const client = new SmpClient(async () => {});
  const answer = client.request(OP.read, GROUP.os, ID.osParams);
  await new Promise(setImmediate);
  t.mock.timers.tick(15_000);
  await assert.rejects(answer, SmpTimeout);
});

test('closing the link fails the request in flight', async () => {
  const client = new SmpClient(async () => {});
  const answer = client.request(OP.read, GROUP.os, ID.osParams);
  await new Promise(setImmediate);
  client.close(new Error('gone'));
  await assert.rejects(answer, /gone/);
});

const slicing = payload => {
  const client = new SmpClient(async bytes => {
    const answer = reply(bytes, { r: decode(bytes.subarray(HEADER_BYTES)).d });
    queueMicrotask(() => {
      for (let at = 0; at < answer.length; at += payload) client.receive(answer.slice(at, at + payload));
    });
  });
  return client;
};

test('the echo probe reads the write size off the first slice of its reply', async () => {
  assert.equal(await probeWriteBytes(slicing(495), 2475), 495);
  assert.equal(await probeWriteBytes(slicing(182), 2475), 182);
  assert.equal(await probeWriteBytes(slicing(20), 2475), 20);
});

test('the echo probe never claims more than the largest payload Chrome asks for', async () => {
  assert.equal(await probeWriteBytes(slicing(1000), 2475), LARGEST_ATT_PAYLOAD);
});

test('the echo probe fits a small device buffer, and reads the reply it got whole as a lower bound', async () => {
  assert.equal(await probeWriteBytes(slicing(495), 300), 300);
});

test('a write the browser refuses is told apart from a refusal by the device', async () => {
  const client = new SmpClient(async () => {
    throw new Error('GATT operation failed for unknown reason.');
  });
  await assert.rejects(client.request(OP.read, GROUP.os, ID.osParams), SmpWriteRejected);
});
