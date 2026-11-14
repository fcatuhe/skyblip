import { Controller } from "@hotwired/stimulus"
import { Turbo } from "@hotwired/turbo-rails"
import { Activator, httpServer } from "#skyblip/activation"
import { hasWebBluetooth, joinFleet } from "#skyblip/fleet"
import { factsOf, shortName } from "controllers/briefing/facts"
import { say, sayNotice } from "controllers/briefing/words"

const WORKING = { asking: "words.asking_server", registering: "words.registering", unlocking: "words.registering" }
const RECONNECT = new Set(["cancelled", "refused_on_device", "claimed", "small_mtu", "link_lost"])

export default class extends Controller {
  static targets = ["name", "facts", "hint", "message", "find", "activate", "reconnect", "kept"]
  static values = { challenges: String, registrations: String, devices: String }
  static outlets = ["glass"]

  connect() {
    this.session = joinFleet(() => this.#render())
    this.fleet = this.session.fleet
    this.candidate = this.fleet.units.findLast(unit => (unit.slug.startsWith("activate-") || unit.needsActivation) && unit.linked) ?? null
    this.#render()
  }

  disconnect() {
    this.session.leave()
  }

  async find() {
    this.failure = null
    try {
      const unit = await this.fleet.connectNew({ activation: true })
      if (unit) this.candidate = unit
    } catch (error) {
      this.failure = { key: "failed", detail: error.message }
    }
    this.#render()
  }

  activate() {
    this.#activator.activate()
  }

  async reconnect() {
    const unit = this.candidate
    unit.disconnect()
    await new Promise(resolve => {
      const stop = this.fleet.listen(() => {
        if (!unit.linked) resolve(stop())
      })
      if (!unit.linked) resolve(stop())
    })
    await this.fleet.connect(unit.slug)
  }

  get #activator() {
    const token = document.querySelector('meta[name="csrf-token"]')?.content
    const server = httpServer({ challenges: this.challengesValue, registrations: this.registrationsValue, token })
    return this.candidate.activator(({ device, onChange }) => new Activator({ server, device, onChange }))
  }

  #render() {
    const unit = this.candidate
    if (unit?.state?.address) return this.#activated(unit)
    const linked = unit?.linked ?? false
    const activator = linked ? this.#activator : null
    if (activator && activator.state.phase === "offline") activator.connect()
    const state = activator?.state
    this.factsTarget.textContent = unit ? [...factsOf(unit), say("words.not_activated")].join(" · ") : ""
    this.nameTarget.textContent = unit && linked ? say("words.activate_unit", { name: shortName(unit) }) : this.nameTarget.dataset.title
    this.hintTarget.hidden = linked
    this.findTarget.hidden = linked || !hasWebBluetooth()
    this.findTarget.disabled = unit?.busy ?? false
    const ready = state?.phase === "ready" && state.read && !state.address
    const notice = state?.notice ?? this.failure ?? unit?.state?.notice
    this.activateTarget.hidden = !ready || RECONNECT.has(notice?.key)
    this.keptTarget.hidden = !ready
    this.reconnectTarget.hidden = !(linked && RECONNECT.has(notice?.key))
    this.#message(state, notice)
    this.#glass(unit, state)
  }

  #activated(unit) {
    if (this.leaving) return
    this.leaving = true
    Turbo.visit(`${this.devicesValue}/${unit.slug}?activated=1`, { action: "replace" })
  }

  #message(state, notice) {
    const text = !hasWebBluetooth() ? say("activation_notices.no_bluetooth")
      : WORKING[state?.phase] ? say(WORKING[state.phase])
      : notice && notice.key !== "activated" ? sayNotice(notice, "activation_notices", "notices")
      : ""
    this.messageTarget.hidden = !text
    this.messageTarget.textContent = text
  }

  #glass(unit, state) {
    if (state?.phase === "confirming") this.glassOutlet.show({ title: "ACTIVATE", prompt: "register", product: "Go" })
    else this.glassOutlet.hide()
  }
}
