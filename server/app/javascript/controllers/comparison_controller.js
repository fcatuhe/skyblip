import { Controller } from "@hotwired/stimulus"
import { connect as connectBluetooth, hasWebBluetooth } from "#skyblip/ble"
import { Chart, SAVED_ZOOMS, TileStore, tilesAround } from "#skyblip/chart"
import { gdl90Frames } from "#skyblip/gdl90"
import { hasPosition } from "#skyblip/geo"
import { nmeaLines, parseNmea } from "#skyblip/nmea"
import { hasWebSerial, openSerial } from "#skyblip/serial"
import { LIVE_MS, TrafficPicture } from "#skyblip/traffic"

const SOURCES = ["retro", "bravo"]
const RENDER_MS = 1000

export default class extends Controller {
  static targets = ["link", "unlink", "state", "baud", "own", "chart", "chartState", "saveState", "tally", "aircraft", "attributes", "logState"]
  static values = { tiles: String, attribution: String }

  connect() {
    this.picture = new TrafficPicture(SOURCES)
    this.links = {}
    this.counts = Object.fromEntries(SOURCES.map(source => [source, { ok: 0, bad: 0, lastAt: null }]))
    this.log = []
    this.store = new TileStore(this.tilesValue, {
      onTile: () => this.drawSoon(),
      onError: (error, { z, x, y }) => { this.chartStateTarget.textContent = `chart tile ${z}/${x}/${y}: ${error.message}` }
    })
    this.chart = new Chart(this.chartTarget, this.store, { attribution: this.attributionValue })
    this.centre = this.savedCentre()
    this.chartStateTarget.textContent = `zoom ${this.chart.zoom}`
    this.timer = setInterval(() => this.render(), RENDER_MS)
    this.offerTransports()
    this.showSavedTiles()
    this.render()
  }

  disconnect() {
    clearInterval(this.timer)
    for (const link of Object.values(this.links)) link.close()
    this.links = {}
  }

  async link({ params: { source, transport } }) {
    this.sourced("link", source).disabled = true
    this.state(source, "connecting")
    try {
      const link = transport === "serial" ? await this.openSerial(source) : await this.openBluetooth(source)
      this.links[source] = link
      this.state(source, link.name, "up")
      this.sourced("unlink", source).disabled = false
    } catch (error) {
      this.sourced("link", source).disabled = false
      this.state(source, error.message, "down")
    }
  }

  unlink({ params: { source } }) {
    this.links[source]?.close()
  }

  async openSerial(source) {
    const feedLines = nmeaLines(line => this.hearLine(source, line))
    const feedFrames = gdl90Frames(
      (message, body) => this.hear(source, message, hex(body)),
      () => { this.counts[source].bad += 1 }
    )
    return openSerial({
      baudRate: Number(this.baudTarget.value),
      onBytes: bytes => { feedLines(bytes); feedFrames(bytes) },
      onClose: error => this.closed(source, error)
    })
  }

  async openBluetooth(source) {
    const device = await connectBluetooth({
      onLine: line => this.hearLine(source, line),
      onClose: () => this.closed(source, null)
    })
    return { name: `${device.name} over ${device.transport}`, close: () => device.disconnect() }
  }

  closed(source, error) {
    delete this.links[source]
    this.state(source, error ? error.message : "disconnected", "down")
    this.sourced("link", source).disabled = false
    this.sourced("unlink", source).disabled = true
  }

  hearLine(source, line) {
    const message = parseNmea(line)
    if (message) this.hear(source, message, line)
    else if (line.startsWith("$")) this.counts[source].bad += 1
  }

  hear(source, message, raw) {
    const at = Date.now()
    this.counts[source].ok += 1
    this.counts[source].lastAt = at
    this.picture.record(source, message, at)
    this.log.push({ at, source, raw, message })
  }

  zoomIn() {
    this.zoomBy(1)
  }

  zoomOut() {
    this.zoomBy(-1)
  }

  zoomBy(step) {
    this.chart.zoomBy(step)
    this.chartStateTarget.textContent = `zoom ${this.chart.zoom}`
    this.drawSoon()
  }

  async saveChart(event) {
    event.preventDefault()
    const form = new FormData(event.target)
    const centre = { latitude: Number(form.get("latitude")), longitude: Number(form.get("longitude")) }
    const tiles = SAVED_ZOOMS.flatMap(zoom => tilesAround(centre, Number(form.get("radius")) * 1000, zoom))
    this.centre = centre
    localStorage.setItem("skyblip.comparison.centre", JSON.stringify(centre))
    try {
      const result = await this.store.save(tiles, ({ saved, failed, total }) => {
        this.saveStateTarget.textContent = `${saved + failed} of ${total} tiles, ${failed} failed`
      })
      this.saveStateTarget.textContent = `Saved ${result.saved} of ${result.total} tiles, ${result.failed} failed`
    } catch (error) {
      this.saveStateTarget.textContent = error.message
    }
    this.drawSoon()
  }

  download() {
    const lines = this.log.map(entry => JSON.stringify({ ...entry, at: new Date(entry.at).toISOString() }))
    const url = URL.createObjectURL(new Blob([lines.join("\n") + "\n"], { type: "application/x-ndjson" }))
    const anchor = document.createElement("a")
    anchor.href = url
    anchor.download = `traffic-comparison-${new Date().toISOString().replaceAll(":", "-")}.ndjson`
    anchor.click()
    setTimeout(() => URL.revokeObjectURL(url))
  }

  clear() {
    this.picture = new TrafficPicture(SOURCES)
    this.log = []
    this.render()
  }

  render() {
    const at = Date.now()
    const rows = this.picture.aircraft(at)
    for (const source of SOURCES) this.renderOwn(source, at)
    this.renderTally(this.picture.tally(at))
    this.aircraftTarget.replaceChildren(...rows.map(row => aircraftRow(row, at)))
    for (const table of this.attributesTargets) renderAttributes(table, this.picture.source(table.dataset.source), at)
    this.logStateTarget.textContent = `${this.log.length} entries`
    this.draw(rows)
  }

  renderOwn(source, at) {
    const { own, status } = this.picture.source(source)
    const counts = this.counts[source]
    const fields = {
      Position: hasPosition(own) ? `${own.latitude.toFixed(5)}, ${own.longitude.toFixed(5)}, ${seconds(at - own.at)} s ago` : "no fix",
      "GNSS altitude": [describe(own.altitudeM, " m MSL"), describe(own.geometricAltitudeFt, " ft HAE")].filter(Boolean).join(", ") || "-",
      "Pressure altitude": describe(own.pressureAltitudeFt, " ft") || "-",
      "Speed and track": [describe(own.groundSpeedKt, " kt"), describe(own.trackDeg, "°")].filter(Boolean).join(", ") || "-",
      Status: statusOf(status),
      Messages: `${counts.ok} decoded, ${counts.bad} refused${counts.lastAt ? `, last ${seconds(at - counts.lastAt)} s ago` : ""}`
    }
    this.sourced("own", source).replaceChildren(...Object.entries(fields).flatMap(([term, value]) => [element("dt", term), element("dd", value)]))
  }

  renderTally({ total, everyone, only, live, farthestM }) {
    this.tallyTarget.textContent = `${total} aircraft heard: ${everyone} by both, ${only.retro} by the Retro only, ${only.bravo} by the Bravo only. ` +
      `Now: ${live.retro} on the Retro, ${live.bravo} on the Bravo. ` +
      `Farthest: ${km(farthestM.retro)} km on the Retro, ${km(farthestM.bravo)} km on the Bravo.`
  }

  drawSoon() {
    this.frame ??= requestAnimationFrame(() => {
      this.frame = null
      this.draw(this.picture.aircraft(Date.now()))
    })
  }

  draw(rows) {
    const owns = SOURCES.map(source => ({ source, ...this.picture.source(source).own })).filter(hasPosition)
    const freshest = owns.toSorted((a, b) => b.at - a.at)[0]
    const aircraft = rows.filter(row => row.live.length > 0).map(row => ({
      label: row.callsign ?? row.address,
      points: row.live.map(source => ({ source, ...row.by[source].report })).filter(hasPosition)
    }))
    this.chart.draw({ centre: freshest ?? this.centre, owns, aircraft })
  }

  async showSavedTiles() {
    if (!globalThis.caches) {
      this.saveStateTarget.textContent = "This browser keeps no tiles: Cache Storage needs https or localhost."
      return
    }
    this.saveStateTarget.textContent = `${await this.store.savedCount()} tiles saved in this browser`
  }

  offerTransports() {
    const missing = { serial: !hasWebSerial() && "This browser has no Web Serial: use Chrome or Edge on a computer.",
                      bluetooth: !hasWebBluetooth() && "This browser has no Web Bluetooth: use Chrome or Edge." }
    for (const button of this.linkTargets) {
      const reason = missing[button.dataset.comparisonTransportParam]
      if (reason) {
        button.disabled = true
        this.state(button.dataset.source, reason, "down")
      }
    }
  }

  savedCentre() {
    const { latitude, longitude } = this.element.querySelector("form").elements
    const saved = JSON.parse(localStorage.getItem("skyblip.comparison.centre") ?? "null")
    return saved ?? { latitude: Number(latitude.value), longitude: Number(longitude.value) }
  }

  state(source, text, kind = "") {
    const target = this.sourced("state", source)
    target.textContent = text
    target.className = `state ${kind}`.trim()
  }

  sourced(name, source) {
    return this[`${name}Targets`].find(target => target.dataset.source === source)
  }
}

const AIRCRAFT_COLUMNS = [
  (target, at) => seconds(at - target.lastAt),
  target => km(target.report.distanceM),
  target => describe(target.report.bearingDeg, "°"),
  target => describe(target.report.relativeVerticalFt, ""),
  target => describe(target.report.groundSpeedKt, ""),
  target => target.reports,
  target => km(target.farthestM)
]

function aircraftRow(row, at) {
  const tr = document.createElement("tr")
  tr.className = row.live.length > 0 ? "" : "stale"
  const heard = SOURCES.filter(source => row.by[source])
  const first = heard.length > 1 ? `${row.firstBy} by ${seconds(row.leadMs)} s` : row.firstBy
  tr.append(element("td", row.address), element("td", row.callsign ?? ""), element("td", heard.join(" + ")), element("td", first))
  for (const source of SOURCES) {
    const target = row.by[source]
    for (const column of AIRCRAFT_COLUMNS) {
      const cell = element("td", target ? String(column(target, at)) : "")
      if (target && !row.live.includes(source)) cell.className = "stale"
      tr.append(cell)
    }
  }
  return tr
}

function renderAttributes(table, { targets }, at) {
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

function rowOf(cell, values) {
  const tr = document.createElement("tr")
  tr.append(...values.map(value => element(cell, value === null || value === undefined ? "" : String(round(value)))))
  return tr
}

function statusOf(status) {
  if (status.kind === "pflau") return `PFLAU: ${status.rx} heard, GPS ${status.gps}, alarm ${status.alarmLevel}`
  if (status.kind === "heartbeat") return `Heartbeat: GPS ${status.gpsValid ? "valid" : "invalid"}, UTC ${status.utcValid ? "valid" : "invalid"}`
  return "-"
}

function element(name, text) {
  const node = document.createElement(name)
  node.textContent = text
  return node
}

function describe(value, unit) {
  return value === null || value === undefined ? "" : `${Math.round(value)}${unit}`
}

function round(value) {
  return typeof value === "number" && !Number.isInteger(value) ? Number(value.toFixed(5)) : value
}

function km(metres) {
  return metres === null || metres === undefined ? "" : (metres / 1000).toFixed(1)
}

function seconds(ms) {
  return Math.round(ms / 1000)
}

function hex(bytes) {
  return [...bytes].map(byte => byte.toString(16).padStart(2, "0")).join("")
}
