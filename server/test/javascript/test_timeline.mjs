import test from 'node:test';
import assert from 'node:assert/strict';

import { TIMELINE_WINDOW_S, timelineSpan } from '#skyblip/timeline';

const lane = (...seconds) => ({ source: 'retro', samples: seconds.map(second => ({ second })) });

test('the timeline shows at least the last minute, from the first second either receiver listed the aircraft', () => {
  assert.deepEqual(timelineSpan([lane(990)], 1000), { from: 940, to: 1000 });
  assert.deepEqual(timelineSpan([lane(700), lane(800)], 1000), { from: 700, to: 1000 });
});

test('an aircraft listed for longer than the window shows only its last part', () => {
  assert.deepEqual(timelineSpan([lane(0)], 5000), { from: 5000 - TIMELINE_WINDOW_S, to: 5000 });
});
