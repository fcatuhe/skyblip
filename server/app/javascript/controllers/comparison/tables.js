import { distanceM, hasPosition } from "#skyblip/geo"
import { LIVE_MS } from "#skyblip/traffic"

export const SOURCES = ["retro", "bravo"]

const AIRCRAFT_COLUMNS = [
  (target, signal, at) => seconds(at - target.lastAt),
  target => km(target.report.distanceM),
  target => describe(target.report.relativeVerticalFt, ""),
  (target, signal) => percent(signal.coverage),
  (target, signal) => signal.dropouts,
  (target, signal) => (signal.freshPerMinute === null ? "" : signal.freshPerMinute.toFixed(1)),
  (target, signal) => describe(signal.rssi?.last, "")
]

export function aircraftRow(row, at, focus) {
  const tr = document.createElement("tr")
  tr.className = [row.live.length > 0 ? "" : "stale", row.address === focus ? "selected" : ""].join(" ").trim()
  const pick = element("button", row.address)
  pick.type = "button"
  pick.className = "pick"
  pick.dataset.action = "comparison#focus"
  pick.dataset.comparisonAddressParam = row.address
  const address = document.createElement("td")
  address.append(pick)
  const heard = SOURCES.filter(source => row.by[source])
  tr.append(address, element("td", row.callsign ?? ""), element("td", heard.join(" + ")), element("td", firstOf(row)))
  for (const source of SOURCES) {
    const target = row.by[source]
    for (const column of AIRCRAFT_COLUMNS) {
      const cell = element("td", target ? String(column(target, row.signal[source], at)) : "")
      if (target && !row.live.includes(source)) cell.className = "stale"
      tr.append(cell)
    }
  }
  return tr
}

export function renderAttributes(table, { targets }, at) {
  const decoded = [...targets.values()].sort((a, b) => b.lastAt - a.lastAt)
  const keys = [...new Set(decoded.flatMap(target => Object.keys(target.decoded)))].filter(key => key !== "kind")
  const head = document.createElement("thead")
  head.append(rowOf("th", ["Age, s", "Reports", ...keys]))
  const body = document.createElement("tbody")
  body.append(...decoded.map(target => {
    const tr = rowOf("td", [seconds(at - target.lastAt), target.reports, ...keys.map(key => target.decoded[key])])
    if (at - target.lastAt > LIVE_MS) tr.className = "stale"
    return tr
  }))
  table.replaceChildren(head, body)
}

export function focusTitle(row) {
  const category = SOURCES.map(source => row.by[source]?.report.category).find(Boolean)
  return [row.callsign, row.address, category].filter(Boolean).join(" · ")
}

export function focusSummary(row, reference) {
  const heard = SOURCES.filter(source => row.by[source])
  const now = row.live.length > 0 ? `listed now by ${row.live.join(" and ")}` : "listed by neither now"
  const from = reference ? `Distances from the ${reference.source} fix.` : "No fix to measure distances from."
  return `Heard by ${heard.join(" and ")}, first: ${firstOf(row)}, ${now}. ${from}`
}

export function focusRows(row, at) {
  const [retro, bravo] = SOURCES.map(source => row.by[source])
  const [retroSignal, bravoSignal] = SOURCES.map(source => row.signal[source])
  return [
    textRow("Last heard", target => `${seconds(at - target.lastAt)} s ago`, retro, bravo),
    textRow("First heard", target => clock(target.firstAt), retro, bravo,
      retro && bravo ? `${seconds(retro.firstAt - bravo.firstAt)} s` : ""),
    signalRow("Listed", signal => `${signal.listedS} s, ${percent(signal.coverage)}`, retroSignal, bravoSignal),
    signalRow("Dropouts", signal => `${signal.dropouts}, longest gap ${signal.longestGapS} s`, retroSignal, bravoSignal),
    signalRow("New positions", signal => (signal.freshCount === null
      ? "not told apart in NMEA"
      : `${signal.freshCount}, ${signal.freshPerMinute?.toFixed(1) ?? "-"}/min, longest wait ${Math.round(signal.longestFreshGapS)} s`), retroSignal, bravoSignal),
    signalRow("RSSI", signal => (signal.rssi
      ? `${signal.rssi.last} dBm, mean ${signal.rssi.mean.toFixed(1)}, ${signal.rssi.min} to ${signal.rssi.max}`
      : "not sent"), retroSignal, bravoSignal),
    numberRow("Distance, km", target => target.report.distanceM / 1000, 1, retro, bravo),
    numberRow("Farthest, km", target => target.farthestM / 1000, 1, retro, bravo),
    numberRow("Bearing", target => target.report.bearingDeg, 0, retro, bravo, { angle: true }),
    numberRow("Height above us, ft", target => target.report.relativeVerticalFt, 0, retro, bravo),
    numberRow("Pressure altitude, ft", target => target.report.altitudeFt, 0, retro, bravo),
    numberRow("Ground speed, kt", target => target.report.groundSpeedKt, 0, retro, bravo),
    numberRow("Track", target => target.report.trackDeg, 0, retro, bravo, { angle: true }),
    numberRow("Climb, fpm", target => target.report.climbFpm, 0, retro, bravo),
    textRow("Category", target => target.report.category ?? "", retro, bravo),
    textRow("Alarm level", target => target.report.alarmLevel ?? "", retro, bravo),
    textRow("Position", target => (hasPosition(target.report)
      ? `${target.report.latitude.toFixed(5)}, ${target.report.longitude.toFixed(5)}`
      : "no fix to place it"), retro, bravo,
    retro && bravo && hasPosition(retro.report) && hasPosition(bravo.report) ? `${Math.round(distanceM(retro.report, bravo.report))} m apart` : "")
  ]
}

function textRow(label, format, retro, bravo, difference = "") {
  return rowOf("td", [label, retro ? format(retro) : "", bravo ? format(bravo) : "", difference], { header: true })
}

function signalRow(label, format, retro, bravo) {
  return rowOf("td", [label, retro ? format(retro) : "", bravo ? format(bravo) : "", ""], { header: true })
}

function numberRow(label, value, digits, retro, bravo, { angle = false } = {}) {
  const [a, b] = [retro, bravo].map(target => (target ? value(target) : null)).map(number => (Number.isFinite(number) ? number : null))
  let difference = a !== null && b !== null ? a - b : null
  if (difference !== null && angle) difference = ((difference + 540) % 360) - 180
  const format = number => (number === null ? "" : number.toFixed(digits))
  const signed = difference === null ? "" : `${difference > 0 ? "+" : ""}${format(difference)}`
  return rowOf("td", [label, format(a), format(b), signed], { header: true })
}

function firstOf(row) {
  if (row.leadMs === null) return row.firstBy
  return row.leadMs < 1000 ? "both within 1 s" : `${row.firstBy}, ${seconds(row.leadMs)} s ahead`
}

function rowOf(cell, values, { header = false } = {}) {
  const tr = document.createElement("tr")
  tr.append(...values.map((value, index) => {
    const node = element(header && index === 0 ? "th" : cell, value === null || value === undefined ? "" : String(round(value)))
    if (header && index === 0) node.scope = "row"
    return node
  }))
  return tr
}

export function element(name, text) {
  const node = document.createElement(name)
  node.textContent = text
  return node
}

export function describe(value, unit) {
  return value === null || value === undefined ? "" : `${Math.round(value)}${unit}`
}

export function km(metres) {
  return metres === null || metres === undefined ? "" : (metres / 1000).toFixed(1)
}

export function seconds(ms) {
  return Math.round(ms / 1000)
}

function percent(ratio) {
  return ratio === null ? "" : `${Math.round(ratio * 100)}%`
}

function clock(at) {
  return new Date(at).toTimeString().slice(0, 8)
}

function round(value) {
  return typeof value === "number" && !Number.isInteger(value) ? Number(value.toFixed(5)) : value
}
