export const MAX_WIDTH = 32;
export const MAX_HEIGHT = 32;
const WING_RUN = 11;
const HOT_SPOT_MARK = '<';
const SAMPLES = 8;
const REFERENCE_COVERAGE = 0.45;
const THRESHOLDS = [0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65];

const OPERATIONS = [
  'addRowFore', 'removeRowFore', 'addRowAft', 'removeRowAft',
  'addColumns', 'removeColumns', 'widenWings', 'narrowWings',
  'toEven', 'trim', 'clear',
];

export function glyph(pixels, hotSpot = null) {
  const width = pixels[0]?.length ?? 0;
  if (!pixels.length || pixels.length > MAX_HEIGHT || width < 3 || width > MAX_WIDTH || pixels.some(row => row.length !== width)) {
    throw new RangeError(`a glyph is 1 to ${MAX_HEIGHT} rows of 3 to ${MAX_WIDTH} equal columns`);
  }
  const given = Number.isInteger(hotSpot) && hotSpot >= 0 && hotSpot < pixels.length ? hotSpot : null;
  return { pixels: pixels.map(row => row.map(bit => (bit ? 1 : 0))), given };
}

export const width = g => g.pixels[0].length;
export const height = g => g.pixels.length;
export const ink = g => g.pixels.flat().reduce((sum, bit) => sum + bit, 0);

export function hotSpot(g) {
  if (g.given !== null) return g.given;
  const inked = g.pixels.map(row => row.reduce((sum, bit) => sum + bit, 0));
  return inked.indexOf(Math.max(...inked));
}

export const rowsAft = g => height(g) - hotSpot(g);

export function parse(text) {
  const words = [...text.matchAll(/(?<![0-9a-zA-Z])0x([0-9a-fA-F]{1,8})\s*,/g)].map(match => parseInt(match[1], 16));
  if (words.length > 2) {
    const width = Number(text.match(/kGlyphW\s*=\s*(\d+)/)?.[1] ?? MAX_WIDTH);
    const origin = text.match(/kGlyphOriginRow\s*=\s*(\d+)/)?.[1];
    return fromWords(words, width, origin === undefined ? null : Number(origin));
  }

  const lines = text.split('\n').map(line => ({ art: line.match(/[#.]{3,}/)?.[0], marked: line.includes(HOT_SPOT_MARK) }))
    .filter(line => line.art);
  if (lines.length > 2) {
    const width = Math.max(...lines.map(line => line.art.length));
    const marked = lines.findIndex(line => line.marked);
    return glyph(lines.map(line => [...line.art.padEnd(width, '.')].map(char => (char === '#' ? 1 : 0))), marked < 0 ? null : marked);
  }
  throw new RangeError("no glyph: expected rows of '#' and '.' or a kOwnshipRows[] table");
}

export function fromWords(words, width, hotSpot = null) {
  if (!(width >= 3 && width <= MAX_WIDTH) || !words.length || words.length > MAX_HEIGHT) {
    throw new RangeError(`a glyph is 1 to ${MAX_HEIGHT} rows of 3 to ${MAX_WIDTH} columns`);
  }
  return glyph(words.map(word => Array.from({ length: width }, (_, column) => Math.floor(word / 2 ** column) % 2)), hotSpot);
}

export const words = g => g.pixels.map(row => row.reduce((word, bit, column) => word + bit * 2 ** column, 0));
export const hexRows = g => words(g).map(word => word.toString(16).padStart(8, '0'));
const rowArt = row => row.map(bit => (bit ? '#' : '.')).join('');

export function toParams(g) {
  const params = { w: String(width(g)), rows: hexRows(g).join('.') };
  if (g.given !== null) params.hot_spot = String(g.given);
  return params;
}

export function toText(g) {
  const spot = hotSpot(g);
  return g.pixels.map((row, r) => rowArt(row) + (r === spot ? `  ${HOT_SPOT_MARK}` : '')).join('\n');
}

export function toCpp(g) {
  return [
    `constexpr int kGlyphW = ${width(g)};`,
    `constexpr int kGlyphOriginRow = ${hotSpot(g)};`,
    'constexpr uint32_t kOwnshipRows[] = {',
    ...g.pixels.map((row, r) => `    0x${hexRows(g)[r]},  // ${rowArt(row)}`),
    '};',
  ].join('\n');
}

export function toCommand(g, typeCode) {
  return JSON.stringify({ cmd: 'ownship', type: typeCode, w: width(g), o: hotSpot(g), rows: hexRows(g).join('') });
}

export function problems(g, limits) {
  const w = width(g), h = height(g), spot = hotSpot(g), found = [];
  const add = (key, details = {}) => found.push({ key, details });

  if (w % 2) add('odd_width', { width: w });
  if (w > limits.span) add('too_wide', { width: w, max: limits.span });
  if (spot > limits.rowsToNose) add('nose_too_long', { rows: spot, max: limits.rowsToNose });
  if (rowsAft(g) > limits.rows - limits.rowsToNose) add('tail_too_long', { rows: rowsAft(g), max: limits.rows - limits.rowsToNose });

  const unmirrored = g.pixels.reduce((sum, row) => sum + row.filter((bit, c) => bit !== row[w - 1 - c]).length, 0);
  if (unmirrored) add('unmirrored', { count: unmirrored });

  const at = (r, c) => (r >= 0 && r < h && c >= 0 && c < w ? g.pixels[r][c] : 0);
  let loose = 0;
  g.pixels.forEach((row, r) => row.forEach((bit, c) => {
    if (bit && !(at(r - 1, c) + at(r + 1, c) + at(r, c - 1) + at(r, c + 1))) loose++;
  }));
  if (loose) add('loose', { count: loose });

  const inked = g.pixels.map(row => row.some(Boolean));
  const first = inked.indexOf(true), last = inked.lastIndexOf(true);
  if (first >= 0 && inked.slice(first, last + 1).includes(false)) add('detached');

  const spine = w % 2 ? [w >> 1] : [w / 2 - 1, w / 2];
  const broken = g.pixels.filter(row => spine.some(c => !row[c])).length;
  if (broken) add('broken_spine', { count: broken });

  return found;
}

export function reshape(g, operation) {
  return OPERATIONS.includes(operation) ? RESHAPES[operation](g) : g;
}

const blankRow = g => Array(width(g)).fill(0);
const rebuilt = (g, pixels, shift = 0) => glyph(pixels, g.given === null ? null : g.given + shift);

function runs(row) {
  const found = [];
  let start = null;
  [...row, 0].forEach((bit, i) => {
    if (bit && start === null) start = i;
    if (!bit && start !== null) { found.push([start, i - 1]); start = null; }
  });
  return found;
}

function scaledEdge(offset, scale, centre) {
  const half = centre % 1;
  return Math.sign(offset) * (Math.round(Math.abs(offset) * scale - half) + half);
}

function rescaledWings(g, target) {
  if (target < 5 || target > MAX_WIDTH) return g;
  const scale = target / width(g), centre = width(g) / 2, newCentre = target / 2;
  return rebuilt(g, g.pixels.map(row => {
    const out = Array(target).fill(0);
    const inked = row.flatMap((bit, i) => (bit ? [i] : []));
    const wing = inked.length && inked[inked.length - 1] - inked[0] + 1 >= WING_RUN;
    runs(row).forEach(([first, last]) => {
      let left = first - centre, right = last + 1 - centre;
      if (wing) { left = scaledEdge(left, scale, newCentre); right = scaledEdge(right, scale, newCentre); }
      for (let x = newCentre + left; x < newCentre + right; x++) if (x >= 0 && x < target) out[x] = 1;
    });
    return out;
  }));
}

const RESHAPES = {
  addRowFore: g => (height(g) < MAX_HEIGHT ? rebuilt(g, [blankRow(g), ...g.pixels], 1) : g),
  addRowAft: g => (height(g) < MAX_HEIGHT ? rebuilt(g, [...g.pixels, blankRow(g)]) : g),
  removeRowFore: g => (height(g) > 1 ? rebuilt(g, g.pixels.slice(1), -1) : g),
  removeRowAft: g => (height(g) > 1 ? rebuilt(g, g.pixels.slice(0, -1)) : g),
  addColumns: g => (width(g) + 2 <= MAX_WIDTH ? rebuilt(g, g.pixels.map(row => [0, ...row, 0])) : g),
  removeColumns: g => (width(g) - 2 >= 3 ? rebuilt(g, g.pixels.map(row => row.slice(1, -1))) : g),
  widenWings: g => rescaledWings(g, width(g) + 2),
  narrowWings: g => rescaledWings(g, width(g) - 2),
  clear: g => rebuilt(g, g.pixels.map(() => blankRow(g))),

  toEven(g) {
    if (width(g) % 2 === 0 || width(g) < 5) return g;
    const axis = width(g) >> 1;
    return rebuilt(g, g.pixels.map(row => row.slice(0, -1).map((_, x) => (x < axis ? row[x] : row[x + 1]) | (x === axis - 1 || x === axis ? row[axis] : 0))));
  },

  trim(g) {
    let pixels = g.pixels, top = 0;
    while (pixels.length > 1 && !pixels[0].some(Boolean)) { pixels = pixels.slice(1); top++; }
    while (pixels.length > 1 && !pixels[pixels.length - 1].some(Boolean)) pixels = pixels.slice(0, -1);
    while (pixels[0].length > 4 && pixels.every(row => !row[0] && !row[row.length - 1])) pixels = pixels.map(row => row.slice(1, -1));
    return rebuilt(g, pixels, -top);
  },
};

export function planform({ key, name, outline, hotSpot: spot, sources = [] }) {
  const polygon = [...outline, ...[...outline].reverse().map(([x, y]) => [-x, y])];
  const span = 2 * Math.max(...outline.map(([x]) => x));
  const length = Math.max(...outline.map(([, y]) => y));
  return { key, name, outline, polygon, span, length, hotSpot: spot, sources, masks: new Map() };
}

function crossingsAt(p, y) {
  const found = [];
  p.polygon.forEach(([x1, y1], i) => {
    const [x2, y2] = p.polygon[(i + 1) % p.polygon.length];
    if ((y1 <= y) !== (y2 <= y)) found.push(x1 + ((y - y1) * (x2 - x1)) / (y2 - y1));
  });
  return found;
}

function mask(p, w, h) {
  const key = `${w}x${h}`;
  if (!p.masks.has(key)) {
    p.masks.set(key, Array.from({ length: h * SAMPLES }, (_, sy) => {
      const crossings = crossingsAt(p, ((sy + 0.5) * p.length) / (h * SAMPLES));
      return Array.from({ length: w * SAMPLES }, (_, sx) => {
        const x = ((sx + 0.5) * p.span) / (w * SAMPLES) - p.span / 2;
        return crossings.filter(c => c < x).length % 2 === 1;
      });
    }));
  }
  return p.masks.get(key);
}

export function coverage(p, w, h) {
  const inside = mask(p, w, h);
  return Array.from({ length: h }, (_, r) => Array.from({ length: w }, (_, c) => {
    let hits = 0;
    for (let dy = 0; dy < SAMPLES; dy++) for (let dx = 0; dx < SAMPLES; dx++) if (inside[r * SAMPLES + dy][c * SAMPLES + dx]) hits++;
    return hits / (SAMPLES * SAMPLES);
  }));
}

export function reference(p, w, h) {
  return coverage(p, w, h).map(row => row.map(share => (share >= REFERENCE_COVERAGE ? 1 : 0)));
}

export function fidelity(p, g) {
  const inside = mask(p, width(g), height(g));
  let both = 0, drawnOnly = 0, outlineOnly = 0;
  inside.forEach((samples, y) => {
    const row = g.pixels[Math.floor(y / SAMPLES)];
    samples.forEach((covered, x) => {
      const drawn = row[Math.floor(x / SAMPLES)] === 1;
      if (drawn && covered) both++;
      else if (drawn) drawnOnly++;
      else if (covered) outlineOnly++;
    });
  });
  const outlineInk = Math.max(both + outlineOnly, 1);
  return { iou: both / Math.max(both + drawnOnly + outlineOnly, 1), missing: outlineOnly / outlineInk, extra: drawnOnly / outlineInk };
}

export const hotSpotRow = (p, h) => Math.min(Math.max(Math.floor((p.hotSpot / p.length) * h), 0), h - 1);

export function trace(p, w, h) {
  const cells = coverage(p, w, h);
  const candidates = THRESHOLDS.map(threshold => traced(cells, threshold));
  const best = candidates.reduce((a, b) => (fidelity(p, b).iou > fidelity(p, a).iou ? b : a));
  return glyph(best.pixels, hotSpotRow(p, h));
}

function traced(cells, threshold) {
  const w = cells[0].length;
  let rows = cells.map(row => row.map(share => (share >= threshold ? 1 : 0)));
  rows = rows.map(row => row.map((bit, c) => bit | row[w - 1 - c]));
  const spine = w % 2 ? [w >> 1] : [w / 2 - 1, w / 2];
  rows.forEach(row => spine.forEach(c => { row[c] = 1; }));
  rows = rows.map(row => row.map((bit, c) => (c > 0 && c < w - 1 && row[c - 1] && row[c + 1] ? 1 : bit)));
  const at = (r, c) => rows[r]?.[c] ?? 0;
  return glyph(rows.map((row, r) => row.map((bit, c) => (bit && at(r - 1, c) + at(r + 1, c) + at(r, c - 1) + at(r, c + 1) ? 1 : 0))));
}

export function ghostPoints(p, w, h) {
  const scale = Math.min(w / p.span, h / p.length);
  const left = (w - p.span * scale) / 2, top = (h - p.length * scale) / 2;
  return p.polygon.map(([x, y]) => `${(left + (x + p.span / 2) * scale).toFixed(2)},${(top + y * scale).toFixed(2)}`).join(' ');
}

export function runsPath(pixels, left = 0, top = 0) {
  return pixels.flatMap((row, r) => runs(row).map(([a, b]) => `M${left + a} ${top + r}h${b - a + 1}v1h-${b - a + 1}z`)).join('');
}

export function diffPaths(p, g) {
  const ref = reference(p, width(g), height(g));
  const kind = (drawn, covered) => (drawn && covered ? 'both' : drawn ? 'extra' : covered ? 'missing' : null);
  const kinds = g.pixels.map((row, r) => row.map((bit, c) => kind(bit === 1, ref[r][c] === 1)));
  return Object.fromEntries(['both', 'extra', 'missing'].map(k => [k, runsPath(kinds.map(row => row.map(x => (x === k ? 1 : 0))))]));
}
