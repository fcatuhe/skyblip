import { readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';

import { readImage, versionText } from './image.js';

export function shelfEntry(bytes, file, release) {
  const image = readImage(bytes);
  if (!image) throw new Error(`${release.tagName}: ${file} is not an MCUboot image`);
  if (!image.key) throw new Error(`${release.tagName}: ${file} names no signing key`);
  return {
    tag: release.tagName,
    name: release.name || release.tagName,
    channel: release.isPrerelease ? 'development' : 'release',
    version: versionText(image.version),
    build: image.version.build,
    key: image.key,
    published: release.publishedAt,
    file: `${release.tagName}/${file}`,
  };
}

export function ordered(entries) {
  const rank = entry => (entry.channel === 'release' ? 0 : 1);
  return [...entries].sort((a, b) => rank(a) - rank(b) || b.build - a.build);
}

export function stock(dir, releases) {
  const byTag = new Map(releases.map(release => [release.tagName, release]));
  const entries = readdirSync(dir, { withFileTypes: true })
    .filter(entry => entry.isDirectory())
    .map(({ name: tag }) => {
      const release = byTag.get(tag);
      if (!release) throw new Error(`${tag} is on the shelf but not among the releases`);
      const files = readdirSync(join(dir, tag)).filter(file => file.endsWith('.signed.bin'));
      if (files.length !== 1) throw new Error(`${tag}: ${files.length} .signed.bin files, expected one`);
      return shelfEntry(new Uint8Array(readFileSync(join(dir, tag, files[0]))), files[0], release);
    });
  writeFileSync(join(dir, 'shelf.json'), `${JSON.stringify({ images: ordered(entries) }, null, 2)}\n`);
  return entries.length;
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const [dir, releases] = process.argv.slice(2);
  if (!dir || !releases) {
    console.error('usage: node simulator/shelf.mjs <shelf dir> <releases.json>');
    process.exit(2);
  }
  console.log(`${stock(dir, JSON.parse(readFileSync(releases, 'utf8')))} images on the shelf`);
}
