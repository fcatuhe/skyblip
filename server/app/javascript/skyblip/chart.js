import { offset } from '#skyblip/geo';

export const TILE_PIXELS = 256;
export const TILE_ZOOMS = { min: 6, max: 11 };
export const VIEW_ZOOMS = { min: 7, max: 13 };
export const SAVED_ZOOMS = [7, 8, 9, 10, 11];

const EQUATOR_M = 40075016.686;
const RING_STEPS_M = [500, 1000, 2000, 5000, 10000, 20000, 50000, 100000];
const RETRY_MS = 30_000;
const SAVE_CONCURRENCY = 4;

export function worldPixel({ latitude, longitude }, zoom) {
  const size = TILE_PIXELS * 2 ** zoom;
  const sin = Math.sin((latitude * Math.PI) / 180);
  return {
    x: ((longitude + 180) / 360) * size,
    y: (0.5 - Math.log((1 + sin) / (1 - sin)) / (4 * Math.PI)) * size,
  };
}

export function metresPerPixel(latitude, zoom) {
  return (EQUATOR_M * Math.cos((latitude * Math.PI) / 180)) / (TILE_PIXELS * 2 ** zoom);
}

export function tilesAround(centre, radiusM, zoom) {
  const northWest = { latitude: offset(centre, radiusM, 0).latitude, longitude: offset(centre, 0, -radiusM).longitude };
  const southEast = { latitude: offset(centre, -radiusM, 0).latitude, longitude: offset(centre, 0, radiusM).longitude };
  const from = worldPixel(northWest, zoom);
  const to = worldPixel(southEast, zoom);
  const tiles = [];
  for (let x = Math.floor(from.x / TILE_PIXELS); x <= Math.floor(to.x / TILE_PIXELS); x++) {
    for (let y = Math.floor(from.y / TILE_PIXELS); y <= Math.floor(to.y / TILE_PIXELS); y++) tiles.push({ z: zoom, x, y });
  }
  return tiles;
}

export function tileUrl(template, { z, x, y }) {
  return template.replace('{z}', z).replace('{x}', x).replace('{y}', y);
}

export class TileStore {
  constructor(template, { cacheName = 'skyblip-oaci-v1', onTile = () => {}, onError = () => {} } = {}) {
    this.template = template;
    this.cacheName = cacheName;
    this.onTile = onTile;
    this.onError = onError;
    this.bitmaps = new Map();
  }

  bitmap(tile) {
    const key = `${tile.z}/${tile.x}/${tile.y}`;
    const held = this.bitmaps.get(key);
    if (held?.bitmap) return held.bitmap;
    if (held && (held.pending || Date.now() - held.failedAt < RETRY_MS)) return null;
    const entry = { pending: true };
    this.bitmaps.set(key, entry);
    this.load(tile).then(
      bitmap => { Object.assign(entry, { pending: false, bitmap }); this.onTile(); },
      error => { Object.assign(entry, { pending: false, failedAt: Date.now() }); this.onError(error, tile); },
    );
    return null;
  }

  async load(tile) {
    return createImageBitmap(await (await this.response(tile)).blob());
  }

  async response(tile) {
    const url = tileUrl(this.template, tile);
    const cache = await caches.open(this.cacheName);
    const saved = await cache.match(url);
    if (saved) return saved;
    const fetched = await fetch(url, { mode: 'cors' });
    if (!fetched.ok) throw new Error(`answered ${fetched.status}`);
    await cache.put(url, fetched.clone());
    return fetched;
  }

  async save(tiles, onProgress) {
    const queue = [...tiles];
    const result = { saved: 0, failed: 0, total: tiles.length };
    const worker = async () => {
      for (let tile = queue.shift(); tile; tile = queue.shift()) {
        try {
          await this.response(tile);
          result.saved += 1;
        } catch (error) {
          result.failed += 1;
          this.onError(error, tile);
        }
        onProgress(result);
      }
    };
    await Promise.all(Array.from({ length: SAVE_CONCURRENCY }, worker));
    return result;
  }

  async savedCount() {
    return (await (await caches.open(this.cacheName)).keys()).length;
  }
}

export class Chart {
  constructor(canvas, store, { attribution }) {
    this.canvas = canvas;
    this.store = store;
    this.attribution = attribution;
    this.zoom = 10;
  }

  zoomBy(step) {
    this.zoom = Math.min(VIEW_ZOOMS.max, Math.max(VIEW_ZOOMS.min, this.zoom + step));
  }

  draw({ centre, owns, aircraft }) {
    const { canvas } = this;
    const ratio = globalThis.devicePixelRatio || 1;
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    if (canvas.width !== Math.round(width * ratio) || canvas.height !== Math.round(height * ratio)) {
      canvas.width = Math.round(width * ratio);
      canvas.height = Math.round(height * ratio);
    }
    const context = canvas.getContext('2d');
    context.setTransform(ratio, 0, 0, ratio, 0, 0);
    const style = getComputedStyle(canvas);
    const colour = name => style.getPropertyValue(`--chart-${name}`).trim();
    const middle = worldPixel(centre, this.zoom);
    const screen = place => {
      const point = worldPixel(place, this.zoom);
      return { x: point.x - middle.x + width / 2, y: point.y - middle.y + height / 2 };
    };

    context.font = `${style.fontWeight} ${style.fontSize} ${style.fontFamily}`;
    this.drawTiles(context, middle, width, height, colour('empty'));
    this.drawRings(context, centre, width, height, colour);
    for (const own of owns) drawOwn(context, screen(own), own.trackDeg, colour(own.source));
    for (const plane of aircraft) drawAircraft(context, plane, screen, colour);
    context.textAlign = 'right';
    halo(context, this.attribution, width - 6, height - 6, colour('label'), colour('halo'));
  }

  drawTiles(context, middle, width, height, empty) {
    const tileZoom = Math.min(this.zoom, TILE_ZOOMS.max);
    const size = TILE_PIXELS * 2 ** (this.zoom - tileZoom);
    const left = middle.x - width / 2;
    const top = middle.y - height / 2;
    context.fillStyle = empty;
    context.fillRect(0, 0, width, height);
    for (let x = Math.floor(left / size); x * size < left + width; x++) {
      for (let y = Math.floor(top / size); y * size < top + height; y++) {
        const bitmap = this.store.bitmap({ z: tileZoom, x, y });
        if (bitmap) context.drawImage(bitmap, x * size - left, y * size - top, size, size);
      }
    }
  }

  drawRings(context, centre, width, height, colour) {
    const perPixel = metresPerPixel(centre.latitude, this.zoom);
    const stepM = RING_STEPS_M.find(step => step / perPixel >= Math.min(width, height) / 8) ?? RING_STEPS_M.at(-1);
    context.textAlign = 'left';
    for (const rings of [1, 2, 3]) {
      const radius = (stepM * rings) / perPixel;
      context.strokeStyle = colour('ring');
      context.lineWidth = 1.5;
      context.beginPath();
      context.arc(width / 2, height / 2, radius, 0, 2 * Math.PI);
      context.stroke();
      halo(context, `${(stepM * rings) / 1000} km`, width / 2 + 4, height / 2 - radius - 4, colour('ring'), colour('halo'));
    }
  }
}

function drawOwn(context, point, trackDeg, stroke) {
  context.save();
  context.translate(point.x, point.y);
  context.rotate(((trackDeg ?? 0) * Math.PI) / 180);
  context.strokeStyle = stroke;
  context.lineWidth = 2;
  context.beginPath();
  context.moveTo(0, -10);
  context.lineTo(0, 10);
  context.moveTo(-8, -2);
  context.lineTo(8, -2);
  context.moveTo(-4, 8);
  context.lineTo(4, 8);
  context.stroke();
  context.restore();
}

function drawAircraft(context, { label, points }, screen, colour) {
  const placed = points.map(point => ({ ...point, ...screen(point) }));
  if (placed.length > 1) {
    context.strokeStyle = colour('link');
    context.lineWidth = 1;
    context.setLineDash([3, 3]);
    context.beginPath();
    context.moveTo(placed[0].x, placed[0].y);
    for (const point of placed.slice(1)) context.lineTo(point.x, point.y);
    context.stroke();
    context.setLineDash([]);
  }
  context.textAlign = 'left';
  placed.forEach((point, index) => {
    drawChevron(context, point, colour(point.source), colour('halo'));
    const text = [index === 0 ? label : null, relativeHundreds(point.relativeVerticalFt)].filter(Boolean).join(' ');
    halo(context, text, point.x + 12, point.y + 4 + index * 14, colour(point.source), colour('halo'));
  });
}

function drawChevron(context, point, stroke, fill) {
  context.save();
  context.translate(point.x, point.y);
  context.lineWidth = 2;
  context.strokeStyle = stroke;
  context.fillStyle = fill;
  context.beginPath();
  if (point.trackDeg === null || point.trackDeg === undefined) {
    context.arc(0, 0, 6, 0, 2 * Math.PI);
  } else {
    context.rotate((point.trackDeg * Math.PI) / 180);
    context.moveTo(0, -11);
    context.lineTo(8, 8);
    context.lineTo(0, 3);
    context.lineTo(-8, 8);
    context.closePath();
  }
  context.fill();
  context.stroke();
  context.restore();
}

function relativeHundreds(feet) {
  if (feet === null || feet === undefined) return null;
  const hundreds = Math.round(feet / 100);
  return `${hundreds >= 0 ? '+' : '-'}${String(Math.abs(hundreds)).padStart(2, '0')}`;
}

function halo(context, text, x, y, fill, outline) {
  context.lineWidth = 3;
  context.strokeStyle = outline;
  context.strokeText(text, x, y);
  context.fillStyle = fill;
  context.fillText(text, x, y);
}
