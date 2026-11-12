import { Controller } from "@hotwired/stimulus"
import { LogReader, fetchDiagnostics } from "#skyblip/log"
import { aircraftOf, isNote, ndjson, typeCounts } from "#skyblip/diagnostics"

const PRESS_TWICE = "The unit asks on its lamp, which blinks green fast: press its button twice to allow, once to refuse."
const REFUSALS = {
  unknown_command: "this unit arms its captures on its glass",
  capturing: "a capture already runs, so stop it first",
  not_capturing: "no capture runs",
  no_storage: "this unit has no capture flash",
  no_diagnostics: "this build writes no diagnostics",
  no_session: "the unit no longer holds that session",
  cancelled: "one press refused it on the unit",
  expired: "nobody answered the unit within 30 seconds",
  in_flight: "the unit refuses it in flight",
  prompt_pending: "the unit is already asking another question",
  claimed: "another connection holds the config endpoint",
  refused_on_device: "the unit refused a question on this link, so disconnect and connect again",
  small_mtu: "this browser kept Bluetooth's smallest packet size, 20 bytes, so disconnect and connect again, or use another browser or computer",
}
const COLUMNS = {
  address: aircraft => aircraft.address,
  adsb_version: aircraft => aircraft.latest.adsb_version,
  pressure_altitude_ft: aircraft => aircraft.latest.pressure_altitude_ft,
  height_difference_ft: aircraft => aircraft.latest.height_difference_ft,
  gnss_relative_altitude_m: aircraft => aircraft.latest.gnss_relative_altitude_m,
  pressure_relative_altitude_m: aircraft => aircraft.latest.pressure_relative_altitude_m,
  records: aircraft => aircraft.records,
  heard: aircraft => aircraft.latest.index,
}

export default class extends Controller {
  static targets = ["panel", "profile", "button", "message", "progress", "results", "session", "types", "download", "header", "aircraft"]
  // INFO: fc 12nov26 blip_capture.py's VERDICT_WAIT_S: the prompt's 30 s, and the pass that reports it expired
  static values = { verdictMs: { type: Number, default: 35000 } }

  connect() {
    this.device = null
    this.reader = null
    this.ordered = null
    this.verdictTimer = null
    this.aircraft = []
    this.order = { column: "heard", descending: true }
    this.panelTarget.hidden = true
    this.forget()
  }

  disconnect() {
    this.closed()
  }

  linked({ detail: { device } }) {
    this.device = device
    this.reader = new LogReader(fields => device.sendLog(fields))
    this.forget()
    this.panelTarget.hidden = !(device.hasConfig && device.hasLog)
  }

  closed() {
    this.reader?.close()
    this.device = null
    this.reader = null
    this.settle()
    this.panelTarget.hidden = true
    this.forget()
  }

  arm(event) {
    event.preventDefault()
    this.capture(this.profileTarget.value)
  }

  stop() {
    this.capture("stop")
  }

  async capture(profile) {
    const { device } = this
    this.ordered = { profile, prompted: false }
    this.hold(true)
    this.awaitVerdict()
    this.say(profile === "stop" ? "Asking the unit to stop its capture." : `Asking the unit to arm a ${profile} capture.`)
    try {
      await device.sendConfig({ command: "capture", profile })
    } catch (error) {
      if (device === this.device) {
        this.settle()
        this.say(error.message, "bad")
      }
    }
  }

  reply({ detail: { frame } }) {
    if (!this.ordered) return
    if (!frame) {
      this.settle()
      return this.say("The unit's verdict did not decode: the config panel shows its bytes.", "bad")
    }
    if (frame.command !== "acknowledgement") return
    // INFO: fc 12nov26 once our prompt stands, prompt_pending answers a command typed in the config panel, not ours
    if (this.ordered.prompted && frame.reason === "prompt_pending") return
    if (frame.pending) {
      this.ordered.prompted = true
      this.awaitVerdict()
      return this.say(PRESS_TWICE, "warn")
    }
    const { profile } = this.ordered
    this.settle()
    if (frame.acknowledge && frame.reason === "capture") {
      this.say(profile === "stop" ? "Capture stopped. Fetch the log to read it." : `${profile} capture armed. Stop it, then fetch the log.`, "ok")
    } else {
      this.say(refusal(frame.reason), "bad")
    }
  }

  log({ detail: { frame, bytes, error } }) {
    this.reader?.receive(frame, bytes, error)
  }

  // INFO: fc 12nov26 a fetch cut by a disconnect settles after the next link, so only the reader it began on may touch the panel
  async fetch() {
    const { reader } = this
    this.hold(true)
    this.say("Reading the diagnostics store.")
    this.progressTarget.removeAttribute("value")
    this.progressTarget.hidden = false
    try {
      const fetched = await fetchDiagnostics(reader, { onProgress: (at, total) => reader === this.reader && this.progressed(at, total) })
      if (reader !== this.reader) return
      if (fetched) {
        this.show(fetched)
        this.say(`Fetched session ${fetched.session.session}.`, "ok")
      } else {
        this.say("The diagnostics store holds no session.", "warn")
      }
    } catch (error) {
      if (reader === this.reader) this.say(error.reason ? refusal(error.reason) : error.message, "bad")
    } finally {
      if (reader === this.reader) {
        this.progressTarget.hidden = true
        this.hold(false)
      }
    }
  }

  progressed(at, total) {
    this.progressTarget.max = total
    this.progressTarget.value = at
  }

  show({ session, lines }) {
    const records = lines.filter(line => !isNote(line)).length
    const torn = lines.find(line => line.type === "torn")
    this.sessionTarget.textContent = [
      `Session ${session.session}: ${records} records, ${session.closed ? "closed" : "not closed"}.`,
      torn ? `Record ${torn.index} dropped: ${torn.reason}.` : "",
      session.truncated ? "Truncated: its first sector was recycled, so it holds no boot and no config record." : "",
    ].filter(Boolean).join(" ")
    this.typesTarget.replaceChildren(...Object.entries(typeCounts(lines)).map(([type, count]) => {
      const item = document.createElement("li")
      item.textContent = `${type} ${count}`
      return item
    }))
    this.forgetDownload()
    this.downloadUrl = URL.createObjectURL(new Blob([ndjson(lines)], { type: "application/x-ndjson" }))
    this.downloadTarget.href = this.downloadUrl
    this.downloadTarget.download = `diagnostics-${session.session}.ndjson`
    this.aircraft = aircraftOf(lines)
    this.resultsTarget.hidden = false
    this.render()
  }

  sort({ params: { column } }) {
    this.order = { column, descending: this.order.column === column ? !this.order.descending : false }
    this.render()
  }

  render() {
    const value = COLUMNS[this.order.column]
    const sign = this.order.descending ? -1 : 1
    const sorted = [...this.aircraft].sort((a, b) => compare(value(a), value(b), sign))
    this.aircraftTarget.replaceChildren(...sorted.map(aircraft => this.row(aircraft)))
    for (const header of this.headerTargets) {
      header.setAttribute("aria-sort", header.dataset.column !== this.order.column ? "none" : this.order.descending ? "descending" : "ascending")
    }
  }

  row({ address, records, latest }) {
    const row = document.createElement("tr")
    const cells = [
      address.toString(16).toUpperCase().padStart(6, "0"), latest.adsb_version, latest.pressure_altitude_ft, latest.height_difference_ft,
      latest.gnss_relative_altitude_m, latest.pressure_relative_altitude_m, records, heardText(latest),
    ]
    row.replaceChildren(...cells.map(text => {
      const cell = document.createElement("td")
      cell.textContent = text ?? "-"
      return cell
    }))
    return row
  }

  awaitVerdict() {
    clearTimeout(this.verdictTimer)
    this.verdictTimer = setTimeout(() => {
      this.settle()
      this.say(`No verdict from the unit within ${this.verdictMsValue / 1000} s.`, "bad")
    }, this.verdictMsValue)
  }

  settle() {
    clearTimeout(this.verdictTimer)
    this.verdictTimer = null
    this.ordered = null
    this.hold(false)
  }

  forget() {
    this.say("-")
    this.progressTarget.hidden = true
    this.resultsTarget.hidden = true
    this.aircraft = []
    this.forgetDownload()
  }

  forgetDownload() {
    if (this.downloadUrl) URL.revokeObjectURL(this.downloadUrl)
    this.downloadUrl = null
    this.downloadTarget.removeAttribute("href")
  }

  hold(busy) {
    for (const button of this.buttonTargets) button.disabled = busy
  }

  say(text, kind = "") {
    this.messageTarget.textContent = text
    this.messageTarget.className = "state" + (kind ? " " + kind : "")
  }
}

function refusal(reason = "no reason given") {
  return REFUSALS[reason] ? `Refused, ${reason}: ${REFUSALS[reason]}.` : `Refused: ${reason}.`
}

function compare(a, b, sign) {
  if (a === b) return 0
  if (a === undefined) return 1
  if (b === undefined) return -1
  return a < b ? -sign : sign
}

function heardText({ at_s, utc_dated }) {
  return utc_dated ? new Date(at_s * 1000).toISOString().slice(11, 19) + " UTC" : `+${at_s} s`
}
