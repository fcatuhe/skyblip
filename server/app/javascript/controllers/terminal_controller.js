import { Controller } from "@hotwired/stimulus"
import { tap } from "#skyblip/ble"
import { joinFleet } from "#skyblip/fleet"
import { lineOf } from "#skyblip/terminal"

// INFO: fc 14nov26 the NMEA endpoint sends several lines a second, so the tape keeps the newest lines only
const KEPT_LINES = 2000
const ARROWS = { in: "<", out: ">" }

export default class extends Controller {
  static targets = ["panel", "tape", "follow"]
  static values = { slug: String, opened: String, closed: String, failed: String }

  connect() {
    this.session = joinFleet(() => this.#listen())
    this.fleet = this.session.fleet
    this.#listen()
  }

  disconnect() {
    this.untap?.()
    this.session.leave()
  }

  clear() {
    this.tapeTarget.replaceChildren()
  }

  // INFO: fc 14nov26 the tap hangs on the device and not on its link, so it hears a reconnect from its first frame
  #listen() {
    const device = this.fleet.unit(this.slugValue)?.handle ?? null
    if (device === this.device) return
    this.untap?.()
    this.device = device
    this.untap = device ? tap(device, entry => this.#print(lineOf(entry))) : null
    this.panelTarget.hidden = !device
  }

  #print(line) {
    const row = document.createElement("span")
    row.className = line.event ? "terminal-line is-event" : `terminal-line is-${line.endpoint} is-${line.direction}`
    if (line.bad) row.classList.add("is-bad")
    const at = Object.assign(document.createElement("span"), { className: "terminal-at", textContent: this.#now() })
    row.append(at, ` ${this.#text(line)}\n`)
    this.tapeTarget.append(row)
    while (this.tapeTarget.childElementCount > KEPT_LINES) this.tapeTarget.firstElementChild.remove()
    if (this.followTarget.checked) this.tapeTarget.scrollTop = this.tapeTarget.scrollHeight
  }

  #text({ event, endpoint, direction, text }) {
    const words = { open: this.openedValue, close: this.closedValue, failed: this.failedValue }
    if (event) return `-- ${[words[event], text].filter(Boolean).join(": ")}`
    return `${endpoint.padEnd(6)} ${ARROWS[direction]} ${text}`
  }

  #now() {
    const now = new Date()
    return `${now.toTimeString().slice(0, 8)}.${String(now.getMilliseconds()).padStart(3, "0")}`
  }
}
