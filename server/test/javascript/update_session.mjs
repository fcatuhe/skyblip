import { FakeSkyblip, signedImage } from './fake_skyblip.mjs';
import { Updater } from '#skyblip/update';

export const NEWER = { major: 0, minor: 2, revision: 0, build: 15 };
export const IMAGE_BYTES = 6000;

export function session(options = {}) {
  const watchers = [];
  const updater = new Updater({
    ...options,
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

export const READS = new Set(['update', 'install_report', 'protocols', 'status', 'get', 'defaults']);
export const asking = task => state => state.phase === 'confirming' && state.task === task;
export const installing = state => state.phase === 'installing';
export const settled = state => state.phase === 'ready' && state.notice;

export async function connected({ returnEveryMs, returnWithinMs, browserKey, ...options } = {}) {
  const device = new FakeSkyblip({ bufSize: 300, ...options }).install();
  const { updater, until } = session({ returnEveryMs, returnWithinMs, browserKey });
  await updater.connect();
  await until(state => state.status && state.running);
  return { device, updater, until };
}

export async function unexchanged(options = {}) {
  const device = new FakeSkyblip({ bufSize: 300, mtu: 23, ...options }).install();
  const { updater, until } = session();
  await updater.connect();
  await until(state => state.status && state.running);
  await updater.choose(signedImage(NEWER, IMAGE_BYTES), 'skyblip_go-0.2.0+15.signed.bin');
  return { device, updater, until };
}

export async function chosen(options) {
  const link = await connected(options);
  await link.updater.choose(signedImage(NEWER, IMAGE_BYTES), 'skyblip_go-0.2.0+15.signed.bin');
  return link;
}

