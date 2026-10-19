import { Controller } from "@hotwired/stimulus"

const LINKED = new Set(["ready", "asking", "confirming", "registering", "unlocking"])
const PHASE_STEP = { asking: "ask", confirming: "confirm", registering: "register", unlocking: "register" }

export default class extends Controller {
  static targets = ["unsupported", "connect", "disconnect", "hint", "activate", "device", "address", "step", "message"]
  static values = { src: String, challenges: String, registrations: String, none: String }

  async connect() {
    const { Activator, hasWebBluetooth, httpServer } = await import(this.srcValue)
    if (!this.element.isConnected) return
    this.bluetooth = hasWebBluetooth()
    this.unsupportedTarget.hidden = this.bluetooth
    const token = document.querySelector('meta[name="csrf-token"]')?.content
    const server = httpServer({ challenges: this.challengesValue, registrations: this.registrationsValue, token })
    this.activator = new Activator({ server, onChange: state => this.#render(state) })
    this.#render(this.activator.state)
  }

  disconnect() {
    this.activator?.disconnect()
  }

  link() {
    this.activator.connect()
  }

  unlink() {
    this.activator.disconnect()
  }

  activate() {
    this.activator.activate()
  }

  #render(state) {
    const linked = LINKED.has(state.phase)
    this.connectTarget.hidden = linked
    this.connectTarget.disabled = !this.bluetooth || state.phase === "connecting"
    this.disconnectTarget.hidden = !linked
    this.hintTarget.hidden = linked
    this.activateTarget.disabled = state.phase !== "ready" || !state.read
    this.deviceTarget.textContent = state.device || "-"
    this.addressTarget.textContent = state.read ? state.address ?? this.noneValue : "-"
    this.addressTarget.classList.toggle("mono", Boolean(state.address))
    const current = PHASE_STEP[state.phase]
    for (const step of this.stepTargets) {
      if (step.dataset.step === current) step.setAttribute("aria-current", "step")
      else step.removeAttribute("aria-current")
    }
    this.messageTarget.textContent = this.#message(state, current)
  }

  #message({ notice, phase }, step) {
    if (notice) return [this.#word(notice.key), notice.detail].filter(Boolean).join(" ")
    if (step) return this.#word(`step_${step}`)
    return phase === "connecting" ? this.#word("connecting") : ""
  }

  #word(key) {
    const word = document.querySelector(`[data-activate-word="${CSS.escape(key)}"]`)
    if (word) return word.textContent.replace(/[ \t\r\n]+/g, " ").trim()
    return key === "refused" ? key : `${this.#word("refused")} ${key}`
  }
}
