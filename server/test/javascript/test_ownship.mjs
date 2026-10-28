import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { encodeFrame } from '#skyblip/link';
import * as ownship from '#skyblip/ownship';

const LIMITS = { span: 24, rows: 16, rowsToNose: 5 };
const CASES = JSON.parse(readFileSync(new URL('../fixtures/files/ownship_checks.json', import.meta.url)));
const art = g => g.pixels.map(row => row.map(bit => (bit ? '#' : '.')).join('')).join('\n');

// The Rails suite reads the same file, so the two checkers cannot drift apart.
test('each shared case reads and checks as the fixture says', () => {
  for (const example of CASES) {
    const g = ownship.parse(example.text);
    assert.equal(ownship.hotSpot(g), example.hot_spot, example.why);
    assert.deepEqual(ownship.problems(g, LIMITS), example.problems, example.why);
  }
});

test('the text version marks the hot spot and reads back to the same drawing', () => {
  const g = ownship.parse(CASES[0].text);
  assert.equal(ownship.toText(g).split('\n')[5], '########################  <');
  assert.deepEqual(ownship.parse(ownship.toText(g)), g);
  assert.deepEqual(ownship.parse(ownship.toCpp(g)), g);
});

test('the address keeps the leftmost column in the lowest bit, and the hot spot only when given', () => {
  const g = ownship.fromWords([1, 6, 1], 3, 1);
  assert.deepEqual(g.pixels, [[1, 0, 0], [0, 1, 1], [1, 0, 0]]);
  assert.deepEqual(ownship.toParams(g), { w: '3', rows: '00000001.00000006.00000001', hot_spot: '1' });
  assert.equal(ownship.toParams(ownship.fromWords([2, 7, 2], 3)).hot_spot, undefined);
  assert.throws(() => ownship.fromWords(Array(200).fill(1), 200000), RangeError);
});

test('the Bluetooth command is the registry\'s ownship frame, every row four bytes of one byte string', () => {
  const g = ownship.parse(CASES[0].text);
  const command = ownship.toCommand(g, 10);
  const fields = JSON.parse(command.text);
  assert.deepEqual(Object.keys(fields), ['command', 'aircraft_type', 'width', 'hot_spot', 'rows']);
  assert.deepEqual(fields, { command: 'ownship', aircraft_type: 10, width: 24, hot_spot: ownship.hotSpot(g), rows: ownship.hexRows(g).join('') });
  assert.equal(command.bytes, encodeFrame(fields).length);
});

// The tallest and widest drawing the editor allows must fit the 182 bytes an iPhone link carries.
test('the widest command is 143 bytes of CBOR, one write on the narrowest link', () => {
  const widest = ownship.glyph(Array.from({ length: ownship.MAX_HEIGHT }, () => Array(ownship.MAX_WIDTH).fill(1)), ownship.MAX_HEIGHT - 1);
  // 1 + command 25 in 3 + type 2 + width 32 in 3 + hot spot 31 in 3 + rows 1 + 2 + 32 x 4.
  assert.equal(ownship.toCommand(widest, 17).bytes, 143);
});

test('rows added or removed at the nose move the hot spot with the aircraft', () => {
  const g = ownship.parse('..##..\n######  <\n..##..');
  assert.equal(ownship.hotSpot(ownship.reshape(g, 'addRowFore')), 2);
  assert.equal(ownship.hotSpot(ownship.reshape(g, 'removeRowFore')), 0);
  assert.equal(ownship.height(ownship.reshape(g, 'addRowAft')), 4);
  assert.equal(ownship.reshape(g, 'destroy'), g);
});

test('trim drops blank edges, columns in pairs so the axis stays', () => {
  const g = ownship.parse('..........\n....##....\n..######..  <\n....##....\n..........');
  const trimmed = ownship.reshape(g, 'trim');
  assert.equal(art(trimmed), '..##..\n######\n..##..');
  assert.equal(ownship.hotSpot(trimmed), 1);
});

test('an odd glyph becomes even about the same axis, a one pixel spine becoming a pair', () => {
  assert.equal(art(ownship.reshape(ownship.parse('..#..\n#####\n..#..'), 'toEven')), '.##.\n####\n.##.');
});

// Each half moves half a pixel towards the axis, so a run off it keeps every pixel.
test('a run off the axis keeps its length when the glyph becomes even', () => {
  assert.equal(art(ownship.reshape(ownship.parse('.###.###.\n.###.###.\n....#....'), 'toEven')), '.######.\n.######.\n...##...');
});

test('wider wing stretches the wing rows and keeps the fuselage and the tail', () => {
  const g = ownship.reshape(ownship.parse(CASES[0].text), 'widenWings');
  assert.equal(ownship.width(g), 26);
  assert.equal(g.pixels[5].filter(Boolean).length, 26);
  assert.equal(g.pixels[13].filter(Boolean).length, 10);
});

test('a narrower or wider wing stays mirrored, however many times', () => {
  let narrow = ownship.parse(CASES[0].text), wide = narrow;
  for (let i = 0; i < 4; i++) {
    narrow = ownship.reshape(narrow, 'narrowWings');
    wide = ownship.reshape(wide, 'widenWings');
    assert.deepEqual(ownship.problems(narrow, { ...LIMITS, span: 32 }).map(p => p.key), [], `narrowed ${i + 1} times`);
    assert.deepEqual(ownship.problems(wide, { ...LIMITS, span: 32 }).map(p => p.key), [], `widened ${i + 1} times`);
  }
  assert.equal(ownship.width(narrow), 16);
});

test('a reshape at the limits leaves the drawing as it is', () => {
  const tall = ownship.glyph(Array.from({ length: ownship.MAX_HEIGHT }, () => [0, 1, 1, 0]));
  const narrow = ownship.parse('.#.\n###\n.#.');
  assert.equal(ownship.reshape(tall, 'addRowFore'), tall);
  assert.equal(ownship.reshape(tall, 'addRowAft'), tall);
  assert.equal(ownship.reshape(narrow, 'toEven'), narrow);
});

// A plus sign 10 m wide and 10 m long, its bar 2 m deep at 4 to 6 m, its spine 2 m wide.
const PLUS = ownship.planform({
  key: 'plus', name: 'Plus', hotSpot: 5,
  outline: [[0, 0], [1, 0], [1, 4], [5, 4], [5, 6], [1, 6], [1, 10], [0, 10]],
});

test('a trace is symmetric, whole and carries the outline hot spot', () => {
  const traced = ownship.trace(PLUS, 10, 10);
  assert.deepEqual(ownship.problems(traced, LIMITS).map(p => p.key), []);
  assert.equal(art(traced).split('\n')[4], '##########');
  assert.equal(ownship.hotSpot(traced), 5);
});

test('the overlap score is whole for the outline itself and counts what is drawn outside it', () => {
  const exact = ownship.glyph(ownship.reference(PLUS, 10, 10));
  assert.equal(ownship.fidelity(PLUS, exact).iou, 1);
  const full = ownship.glyph(Array.from({ length: 10 }, () => Array(10).fill(1)));
  // The plus covers 20 + 16 = 36 of 100 m², so a full square draws 64 outside it.
  assert.ok(Math.abs(ownship.fidelity(PLUS, full).extra - 64 / 36) < 1e-9);
});

test('the ghost keeps the outline proportions inside a grid of another shape', () => {
  const [xs, ys] = [0, 1].map(axis => ownship.ghostPoints(PLUS, 20, 10).split(' ').map(point => Number(point.split(',')[axis])));
  assert.equal(Math.max(...xs) - Math.min(...xs), 10);
  assert.equal(Math.max(...ys) - Math.min(...ys), 10);
});

test('the sprite path runs each inked stretch of a row as one rectangle', () => {
  assert.equal(ownship.runsPath([[0, 1, 1], [1, 0, 0]]), 'M1 0h2v1h-2zM0 1h1v1h-1z');
});
