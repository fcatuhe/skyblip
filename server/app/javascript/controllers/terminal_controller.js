import { Controller } from "@hotwired/stimulus"
import { tap } from "#skyblip/ble"
import { joinFleet } from "#skyblip/fleet"
import { lineOf } from "#skyblip/terminal"

// INFO: fc 14nov26 the NMEA endpoint sends several lines a second, so the tape keeps the newest lines only
const KEPT_LINES = 2000
const ARROWS = { in: "<", out: ">" }

export default class extends Controller {
  static targets = ["panel", "tape", "pause"]
  static values = { slug: String, opened: String, closed: String, failed: String, resume: String }

  connect() {
    this.held = []
    this.session = joinFleet(() => this.#listen())
    this.fleet = this.session.fleet
    this.#listen()
  }

  disconnect() {
    this.untap?.()
    this.session.leave()
  }

  // INFO: fc 14nov26 a paused tape holds what arrives, so the line being read never moves under the reader
  pause() {
    this.paused = !this.paused
    if (!this.paused) this.#show(...this.held.splice(0))
    this.#pauseLabel()
  }

  clear() {
    this.tapeTarget.replaceChildren()
    this.held = []
    this.#pauseLabel()
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
    if (this.paused) {
      this.held.push(row)
      if (this.held.length > KEPT_LINES) this.held.shift()
      this.#pauseLabel()
    } else {
      this.#show(row)
    }
  }

  #show(...rows) {
    this.tapeTarget.append(...rows)
    while (this.tapeTarget.childElementCount > KEPT_LINES) this.tapeTarget.firstElementChild.remove()
    this.tapeTarget.scrollTop = this.tapeTarget.scrollHeight
  }

  #pauseLabel() {
    const button = this.pauseTarget
    button.setAttribute("aria-pressed", String(Boolean(this.paused)))
    button.textContent = this.paused ? this.resumeValue.replace("%{count}", this.held.length) : button.dataset.label
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
