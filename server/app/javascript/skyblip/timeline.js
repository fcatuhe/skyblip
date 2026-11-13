export const TIMELINE_WINDOW_S = 15 * 60;

const LABEL_PIXELS = 64;
const LANE_PIXELS = 16;
const GAP_PIXELS = 10;
const DISTANCE_STEPS_KM = [1, 2, 5, 10, 20, 50, 100, 200];

export function timelineSpan(lanes, nowS) {
  const firsts = lanes.flatMap(({ samples }) => (samples.length > 0 ? [samples[0].second] : []));
  const from = Math.max(nowS - TIMELINE_WINDOW_S, Math.min(nowS - 60, ...firsts));
  return { from, to: nowS };
}

export function drawTimeline(canvas, { lanes, nowS }) {
  const ratio = globalThis.devicePixelRatio || 1;
  const width = canvas.clientWidth;
  const rssiShown = lanes.some(({ samples }) => samples.some(sample => Number.isFinite(sample.rssiDbm)));
  const height = canvas.clientHeight;
  const plotsPixels = height - lanes.length * LANE_PIXELS - GAP_PIXELS - 16 - (rssiShown ? GAP_PIXELS : 0);
  const distancePixels = rssiShown ? plotsPixels * 0.6 : plotsPixels;
  canvas.width = Math.round(width * ratio);
  canvas.height = Math.round(height * ratio);
  const context = canvas.getContext('2d');
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  const style = getComputedStyle(canvas);
  const colour = name => style.getPropertyValue(`--chart-${name}`).trim();
  context.font = `${style.fontSize} ${style.fontFamily}`;
  context.textBaseline = 'middle';

  const { from, to } = timelineSpan(lanes, nowS);
  const plotWidth = width - LABEL_PIXELS - 8;
  const x = second => LABEL_PIXELS + ((second - from) / Math.max(1, to - from)) * plotWidth;
  const secondPixels = Math.max(1, plotWidth / Math.max(1, to - from));
  context.clearRect(0, 0, width, height);

  let top = 0;
  for (const { source, label, samples } of lanes) {
    context.fillStyle = colour('timeline-text');
    context.fillText(label, 0, top + LANE_PIXELS / 2);
    context.fillStyle = colour('timeline-empty');
    context.fillRect(LABEL_PIXELS, top + 3, plotWidth, LANE_PIXELS - 6);
    for (const sample of samples) {
      if (sample.second < from) continue;
      context.fillStyle = colour(source);
      context.globalAlpha = sample.fresh ? 1 : 0.45;
      const tall = sample.fresh ? 0 : 3;
      context.fillRect(x(sample.second), top + tall, secondPixels, LANE_PIXELS - 2 * tall);
    }
    context.globalAlpha = 1;
    top += LANE_PIXELS;
  }

  top += GAP_PIXELS;
  const distances = lanes.flatMap(({ samples }) => samples.map(sample => sample.distanceM).filter(Number.isFinite));
  const ceilingKm = DISTANCE_STEPS_KM.find(step => step * 1000 >= Math.max(0, ...distances)) ?? DISTANCE_STEPS_KM.at(-1);
  plot(context, { lanes, top, height: distancePixels, from, x, colour, label: `${ceilingKm} km`, floorLabel: '0 km',
    value: sample => sample.distanceM, floor: 0, ceiling: ceilingKm * 1000 });
  top += distancePixels;

  if (rssiShown) {
    top += GAP_PIXELS;
    const levels = lanes.flatMap(({ samples }) => samples.map(sample => sample.rssiDbm).filter(Number.isFinite));
    const floor = Math.floor(Math.min(...levels) / 10) * 10;
    const ceiling = Math.max(floor + 10, Math.ceil(Math.max(...levels) / 10) * 10);
    plot(context, { lanes, top, height: plotsPixels - distancePixels, from, x, colour, label: `${ceiling} dBm`, floorLabel: `${floor} dBm`,
      value: sample => sample.rssiDbm, floor, ceiling });
    top += plotsPixels - distancePixels;
  }

  context.fillStyle = colour('timeline-text');
  context.textAlign = 'left';
  context.fillText(`${Math.round((to - from) / 60)} min ago`, LABEL_PIXELS, top + 8);
  context.textAlign = 'right';
  context.fillText('now', width - 8, top + 8);
  context.textAlign = 'left';
}

function plot(context, { lanes, top, height, from, x, colour, label, floorLabel, value, floor, ceiling }) {
  const y = level => top + height - ((level - floor) / (ceiling - floor)) * height;
  context.strokeStyle = colour('timeline-grid');
  context.lineWidth = 1;
  context.strokeRect(LABEL_PIXELS, top, context.canvas.clientWidth - LABEL_PIXELS - 8, height);
  context.fillStyle = colour('timeline-text');
  context.fillText(label, 0, top + 6);
  context.fillText(floorLabel, 0, top + height - 6);
  for (const { source, samples } of lanes) {
    const points = samples.filter(sample => sample.second >= from && Number.isFinite(value(sample)));
    context.strokeStyle = colour(source);
    context.fillStyle = colour(source);
    context.lineWidth = 2;
    context.beginPath();
    points.forEach((sample, index) => {
      const joined = index > 0 && sample.second - points[index - 1].second <= 1;
      if (joined) context.lineTo(x(sample.second), y(value(sample)));
      else context.moveTo(x(sample.second), y(value(sample)));
    });
    context.stroke();
    for (const sample of points) context.fillRect(x(sample.second) - 1.5, y(value(sample)) - 1.5, 3, 3);
  }
}
