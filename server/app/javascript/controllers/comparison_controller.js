import { Controller } from "@hotwired/stimulus"
import { connect as connectBluetooth, hasWebBluetooth } from "#skyblip/ble"
import { Chart, SAVED_ZOOMS, TileStore, tilesAround } from "#skyblip/chart"
import { gdl90Frames } from "#skyblip/gdl90"
import { hasPosition } from "#skyblip/geo"
import { nmeaLines, parseNmea } from "#skyblip/nmea"
import { hasWebSerial, openSerial } from "#skyblip/serial"
import { drawTimeline } from "#skyblip/timeline"
import { TrafficPicture } from "#skyblip/traffic"
import {
  SOURCES, aircraftRow, describe, element, focusRows, focusSummary, focusTitle, km, renderAttributes, seconds
} from "controllers/comparison/tables"

const REFERENCE_ORDER = ["bravo", "retro"]
const RENDER_MS = 1000

export default class extends Controller {
  static targets = [
    "link", "unlink", "state", "baud", "own", "chart", "chartState", "saveState", "tally", "aircraft", "attributes", "logState",
    "focusEmpty", "focusBody", "focusTitle", "focusSummary", "focusRows", "timeline"
  ]
  static values = { tiles: String, attribution: String }

  connect() {
    this.picture = new TrafficPicture(SOURCES, { referenceOrder: REFERENCE_ORDER })
    this.focused = null
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

  focus({ params: { address } }) {
    this.focused = address
    this.render()
  }

  pick(event) {
    const address = this.chart.hit(event.offsetX, event.offsetY)
    if (address) this.focus({ params: { address } })
  }

  unfocus() {
    this.focused = null
    this.render()
  }

  clear() {
    this.picture = new TrafficPicture(SOURCES, { referenceOrder: REFERENCE_ORDER })
    this.focused = null
    this.log = []
    this.render()
  }

  render() {
    const at = Date.now()
    const rows = this.picture.aircraft(at)
    for (const source of SOURCES) this.renderOwn(source, at)
    this.renderTally(this.picture.tally(at), this.picture.reference(at))
    this.aircraftTarget.replaceChildren(...rows.map(row => aircraftRow(row, at, this.focused)))
    for (const table of this.attributesTargets) renderAttributes(table, this.picture.source(table.dataset.source), at)
    this.logStateTarget.textContent = `${this.log.length} entries`
    this.renderFocus(at)
    this.draw(rows, at)
  }

  renderFocus(at) {
    const row = this.focused && this.picture.row(this.focused, at)
    this.focusEmptyTarget.hidden = Boolean(row)
    this.focusBodyTarget.hidden = !row
    if (!row) return
    this.focusTitleTarget.textContent = focusTitle(row)
    this.focusSummaryTarget.textContent = focusSummary(row, this.picture.reference(at))
    this.focusRowsTarget.replaceChildren(...focusRows(row, at))
    const lanes = SOURCES.filter(source => row.by[source]).map(source => ({ source, label: source, samples: row.by[source].samples }))
    drawTimeline(this.timelineTarget, { lanes, nowS: Math.floor(at / 1000) })
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

  renderTally({ total, everyone, only, live, farthestM }, reference) {
    this.tallyTarget.textContent = `${total} aircraft heard: ${everyone} by both, ${only.retro} by the Retro only, ${only.bravo} by the Bravo only. ` +
      `Now: ${live.retro} on the Retro, ${live.bravo} on the Bravo. ` +
      `Farthest: ${km(farthestM.retro)} km on the Retro, ${km(farthestM.bravo)} km on the Bravo. ` +
      (reference ? `Distances from the ${reference.source} fix.` : "No fix yet to measure GDL 90 distances from.")
  }

  drawSoon() {
    this.frame ??= requestAnimationFrame(() => {
      this.frame = null
      const at = Date.now()
      this.draw(this.picture.aircraft(at), at)
    })
  }

  draw(rows, at) {
    const owns = SOURCES.map(source => ({ source, ...this.picture.source(source).own })).filter(hasPosition)
    const aircraft = rows.filter(row => row.live.length > 0).map(row => ({
      address: row.address,
      label: row.callsign ?? row.address,
      points: row.live.map(source => ({ source, ...row.by[source].report })).filter(hasPosition)
    }))
    this.chart.draw({ centre: this.picture.reference(at) ?? this.centre, owns, aircraft, focus: this.focused })
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

function statusOf(status) {
  if (status.kind === "pflau") return `PFLAU: ${status.rx} heard, GPS ${status.gps}, alarm ${status.alarmLevel}`
  if (status.kind === "heartbeat") return `Heartbeat: GPS ${status.gpsValid ? "valid" : "invalid"}, UTC ${status.utcValid ? "valid" : "invalid"}`
  return "-"
}

function hex(bytes) {
  return [...bytes].map(byte => byte.toString(16).padStart(2, "0")).join("")
}
