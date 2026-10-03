import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { DEVELOPMENT_KEY, PRODUCTION_KEY, signedImage } from './fake_skyblip.mjs';
import { ordered, shelfEntry, stock } from './shelf.mjs';

const IMAGE_BYTES = 2000;
const RELEASE = { tagName: 'v0.2.0', name: 'v0.2.0', isPrerelease: false, publishedAt: '2026-10-03T10:00:00Z' };
const MAIN = { tagName: 'dev-900', name: 'skyBlip dev 0.2.0+900', isPrerelease: true, publishedAt: '2026-10-04T10:00:00Z' };
const BRANCH = {
  tagName: 'dev-905-feat-x', name: 'skyBlip dev 0.2.0+905 (feat/x)', isPrerelease: true, publishedAt: '2026-10-05T10:00:00Z',
};

function shelve(dir, release, version, key) {
  mkdirSync(join(dir, release.tagName));
  const file = `skyblip-go-${release.tagName}.signed.bin`;
  writeFileSync(join(dir, release.tagName, file), signedImage(version, IMAGE_BYTES, key));
  return file;
}

test('an image is described by what its header and TLVs say, not by its tag', () => {
  const entry = shelfEntry(signedImage({ major: 0, minor: 2, revision: 0, build: 899 }, IMAGE_BYTES), 'x.signed.bin', RELEASE);
  assert.deepEqual(entry, {
    tag: 'v0.2.0', name: 'v0.2.0', channel: 'release', version: '0.2.0+899', build: 899, key: 'B2B2B2B2',
    published: '2026-10-03T10:00:00Z', file: 'v0.2.0/x.signed.bin',
  });
});

test('a file that is not an image, or names no key, stops the shelf', () => {
  assert.throws(() => shelfEntry(new Uint8Array(64), 'x.signed.bin', MAIN), /not an MCUboot image/);
  const unsigned = signedImage({ major: 0, minor: 2, revision: 0, build: 1 }, IMAGE_BYTES);
  unsigned.fill(0, IMAGE_BYTES - 40);
  assert.throws(() => shelfEntry(unsigned, 'x.signed.bin', MAIN), /names no signing key/);
});

test('the release comes first, then development builds from the newest', () => {
  const names = ordered([
    { name: 'old', channel: 'development', build: 880 },
    { name: 'release', channel: 'release', build: 870 },
    { name: 'new', channel: 'development', build: 905 },
  ]).map(entry => entry.name);
  assert.deepEqual(names, ['release', 'new', 'old']);
});

test('a stocked shelf lists every tag it holds, with the key each was signed with', () => {
  const dir = mkdtempSync(join(tmpdir(), 'shelf-'));
  shelve(dir, RELEASE, { major: 0, minor: 2, revision: 0, build: 870 }, PRODUCTION_KEY);
  shelve(dir, MAIN, { major: 0, minor: 2, revision: 0, build: 900 }, DEVELOPMENT_KEY);
  shelve(dir, BRANCH, { major: 0, minor: 2, revision: 0, build: 905 }, DEVELOPMENT_KEY);
  assert.equal(stock(dir, [RELEASE, MAIN, BRANCH]), 3);
  const { images } = JSON.parse(readFileSync(join(dir, 'shelf.json'), 'utf8'));
  assert.deepEqual(images.map(image => [image.tag, image.key]), [
    ['v0.2.0', 'B2B2B2B2'], ['dev-905-feat-x', 'A1A1A1A1'], ['dev-900', 'A1A1A1A1'],
  ]);
});

test('a directory no release accounts for stops the shelf', () => {
  const dir = mkdtempSync(join(tmpdir(), 'shelf-'));
  shelve(dir, MAIN, { major: 0, minor: 2, revision: 0, build: 900 }, DEVELOPMENT_KEY);
  assert.throws(() => stock(dir, [RELEASE]), /not among the releases/);
});
