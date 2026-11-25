export const AIR_EVENTS = ["tx", "rx", "deaf", "coll"]

const SECOND_MS = 1000
const PLAN_HEIGHT = 30
const LANE_Y = { 869525: 44, 868200: 64, 868400: 84 }
const STRIPES = [
  ["band-slot-one", 0, 200], ["band-uplink", 205, 395], ["band-slot-zero", 400, 800], ["band-slot-one", 800, 1000]
]
const STRIPE_LABELS = [
  ["slot 1 cont., RX only", 6], ["uplink 205-395, O-band", 208], ["slot 0: 868.200", 408], ["slot 1: 868.400 ->", 808]
]
const DIVIDERS_MS = [200, 205, 395, 400, 800]
const OWN_WINDOW_MS = [450, 1000]

function laneOf(frequencyKhz) {
  if (frequencyKhz > 869000) return LANE_Y[869525]
  return frequencyKhz > 868300 ? LANE_Y[868400] : LANE_Y[868200]
}

export function drawBandChart(canvas, bursts, color) {
  const band = canvas.getContext("2d")
  const { width, height } = canvas
  const x = ms => (ms * width) / SECOND_MS
  band.clearRect(0, 0, width, height)

  for (const [tone, from, to] of STRIPES) {
    band.fillStyle = color(tone)
    band.fillRect(x(from), 0, x(to - from), PLAN_HEIGHT)
  }
  band.fillStyle = color("band-text")
  band.font = "12px system-ui"
  for (const [label, at] of STRIPE_LABELS) band.fillText(label, x(at), 19)

  band.strokeStyle = color("band-divider")
  for (const at of DIVIDERS_MS) {
    band.beginPath(); band.moveTo(x(at), 0); band.lineTo(x(at), height); band.stroke()
  }
  band.strokeStyle = color("band-window")
  band.setLineDash([3, 3])
  band.beginPath()
  band.moveTo(x(OWN_WINDOW_MS[0]), 0); band.lineTo(x(OWN_WINDOW_MS[0]), height)
  band.moveTo(x(OWN_WINDOW_MS[1]) - 1, 0); band.lineTo(x(OWN_WINDOW_MS[1]) - 1, height)
  band.stroke()
  band.setLineDash([])

  band.fillStyle = color("band-label")
  band.font = "10px ui-monospace,monospace"
  for (const [khz, y] of Object.entries(LANE_Y)) band.fillText((Number(khz) / 1000).toFixed(3), 8, y - 2)

  const tones = { tx: color("accent"), rx: color("radio-rx"), deaf: color("radio-deaf"), coll: color("radio-collision") }
  for (const { phaseMs, frequencyKhz, event } of bursts) {
    band.fillStyle = tones[event]
    band.fillRect(x(phaseMs) - 1, laneOf(frequencyKhz) - 10, 3, 11)
  }
}
