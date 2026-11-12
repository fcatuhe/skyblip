import test from 'node:test';
import assert from 'node:assert/strict';

import { metresPerPixel, tileUrl, tilesAround, worldPixel } from '#skyblip/chart';

const MERIGNAC = { latitude: 44.8283, longitude: -0.7156 };

// IGN's zoom 10 tile at column 509, row 369 draws Merignac in its top right corner.
test('Merignac falls in the top right corner of the tile IGN draws it on', () => {
  const { x, y } = worldPixel(MERIGNAC, 10);
  assert.deepEqual([Math.floor(x / 256), Math.floor(y / 256)], [509, 369]);
  assert.deepEqual([Math.round(x % 256), Math.round(y % 256)], [247, 12]);
});

test('the saved area covers the radius on every side and nothing far beyond it', () => {
  const tiles = tilesAround(MERIGNAC, 100_000, 11);
  const columns = new Set(tiles.map(tile => tile.x));
  const rows = new Set(tiles.map(tile => tile.y));
  const tileM = 256 * metresPerPixel(MERIGNAC.latitude, 11);
  assert.ok(columns.size * tileM >= 200_000 && columns.size * tileM <= 200_000 + 2 * tileM);
  assert.ok(rows.size >= columns.size - 1 && rows.size <= columns.size + 1);
  assert.equal(tiles.length, columns.size * rows.size);
});

test('a tile URL fills the WMTS template', () => {
  assert.equal(tileUrl('t?M={z}&R={y}&C={x}', { z: 9, x: 254, y: 184 }), 't?M=9&R=184&C=254');
});
