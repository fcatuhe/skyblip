import { Controller } from "@hotwired/stimulus"

const LINKED = new Set(["ready", "asking", "confirming", "registering", "unlocking", "saving", "storing"])
const PHASE_STEP = { asking: "ask", confirming: "confirm", registering: "register", unlocking: "register", saving: "settings", storing: "settings" }

export default class extends Controller {
  static targets = ["unsupported", "connect", "disconnect", "hint", "activate", "device", "address", "step", "message",
                    "settings", "fields", "ground", "save", "skip"]
  static values = { src: String, challenges: String, registrations: String, none: String }

  async connect() {
    const { Activator, hasWebBluetooth, httpServer, readSetting, showSetting } = await import(this.srcValue)
    if (!this.element.isConnected) return
    this.readSetting = readSetting
    this.showSetting = showSetting
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

  edit() {
    this.#offer(this.activator.state)
  }

  save(event) {
    event.preventDefault()
    this.activator.saveSettings(this.#edited())
  }

  skip() {
    this.activator.skipSettings()
  }

  #render(state) {
    const linked = LINKED.has(state.phase)
    this.connectTarget.hidden = linked
    this.connectTarget.disabled = !this.bluetooth || state.phase === "connecting"
    this.disconnectTarget.hidden = !linked
    this.hintTarget.hidden = linked
    this.activateTarget.disabled = state.phase !== "ready" || !state.read || state.offer === "open"
    this.deviceTarget.textContent = state.device || "-"
    this.addressTarget.textContent = state.read ? state.address ?? this.noneValue : "-"
    this.addressTarget.classList.toggle("mono", Boolean(state.address))
    this.#offer(state)
    const current = PHASE_STEP[state.phase]
    for (const step of this.stepTargets) {
      if (step.dataset.step === current) step.setAttribute("aria-current", "step")
      else step.removeAttribute("aria-current")
    }
    this.messageTarget.textContent = this.#message(state, current)
  }

  #offer({ offer, phase, settings, heldForGround }) {
    const open = offer === "open" && Boolean(settings)
    this.settingsTarget.hidden = !open
    if (!open) {
      this.shown = null
      return
    }
    if (settings !== this.shown) this.#fill(settings)
    const idle = phase === "ready"
    const edited = this.#edited()
    const changed = Object.keys(edited).some(key => edited[key] !== settings[key])
    this.fieldsTarget.disabled = !idle
    this.groundTarget.hidden = !heldForGround
    this.saveTarget.disabled = !idle || !changed || heldForGround
    this.skipTarget.disabled = !idle
  }

  #fill(settings) {
    this.shown = settings
    const { elements } = this.settingsTarget
    for (const field of this.settingsTarget.querySelectorAll(".manage-field")) {
      const input = field.querySelector("[name]")
      const held = Boolean(input) && input.name in settings
      field.hidden = !held
      if (input) input.disabled = !held
    }
    for (const [key, value] of Object.entries(settings)) this.showSetting(elements[key], value)
  }

  #edited() {
    const { elements } = this.settingsTarget
    return Object.fromEntries(Object.keys(this.shown ?? {}).map(key => [key, this.readSetting(elements[key])]))
  }

  #message({ notice, phase }, step) {
    if (notice) return [this.#word(notice.key, notice.during), notice.detail].filter(Boolean).join(" ")
    if (step) return this.#word(`step_${step}`)
    return phase === "connecting" ? this.#word("connecting") : ""
  }

  #word(key, during = null) {
    const word = (during && this.#find(`${during}_${key}`)) || this.#find(key)
    if (word) return word
    return key === "refused" ? key : `${this.#word("refused")} ${key}`
  }

  #find(key) {
    const word = document.querySelector(`[data-activate-word="${CSS.escape(key)}"]`)
    return word?.textContent.replace(/[ \t\r\n]+/g, " ").trim()
  }
}
