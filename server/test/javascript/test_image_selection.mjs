import test from 'node:test';
import assert from 'node:assert/strict';
import { signedImage } from './fake_skyblip.mjs';
import { session, NEWER, IMAGE_BYTES } from './update_session.mjs';

test('starting another selection removes the previous installable image', async () => {
  const { updater } = session();
  await updater.choose(signedImage(NEWER, IMAGE_BYTES), 'first.signed.bin');
  assert.equal(updater.state.file.name, 'first.signed.bin');

  updater.clearImage();
  assert.equal(updater.state.file, null);
  await updater.install();
  assert.equal(updater.state.notice.key, 'no_file');
});
