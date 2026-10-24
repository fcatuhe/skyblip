import { FakeSkyblip, signedImage } from './fake_skyblip.mjs';
import { Updater } from '#skyblip/update';

export const NEWER = { major: 0, minor: 2, revision: 0, build: 15 };
export const IMAGE_BYTES = 6000;

export function session() {
  const watchers = [];
  const updater = new Updater({
    onChange: state => {
      for (const watcher of watchers.splice(0)) {
        if (watcher.until(state)) watcher.resolve(state);
        else watchers.push(watcher);
      }
    },
  });
  const until = predicate =>
    new Promise(resolve => {
      if (predicate(updater.state)) resolve(updater.state);
      else watchers.push({ until: predicate, resolve });
    });
  return { updater, until };
}

export const READS = new Set(['update', 'status', 'get', 'defaults']);
export const asking = task => state => state.phase === 'confirming' && state.task === task;
export const installing = state => state.phase === 'installing';
export const settled = state => state.phase === 'ready' && state.notice;

export async function connected(options = {}) {
  const device = new FakeSkyblip({ bufSize: 300, ...options }).install();
  const { updater, until } = session();
  await updater.connect();
  await until(state => state.status && state.running);
  return { device, updater, until };
}

// INFO: fc 20oct26 an unexchanged MTU of 23: no config reply fits its 20 bytes, so only SMP's running version arrives
export async function unexchanged(options = {}) {
  const device = new FakeSkyblip({ bufSize: 300, mtu: 23, ...options }).install();
  const { updater, until } = session();
  await updater.connect();
  await until(state => state.running);
  await updater.choose(signedImage(NEWER, IMAGE_BYTES), 'skyblip_go-0.2.0+15.signed.bin');
  return { device, updater, until };
}

export async function chosen(options) {
  const link = await connected(options);
  await link.updater.choose(signedImage(NEWER, IMAGE_BYTES), 'skyblip_go-0.2.0+15.signed.bin');
  return link;
}

