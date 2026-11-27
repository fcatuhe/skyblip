import { Controller } from "@hotwired/stimulus"
import { Turbo } from "@hotwired/turbo-rails"
import { Activator, httpServer } from "#skyblip/activation"
import { bluetoothReadiness, joinFleet } from "#skyblip/fleet"
import { sharedJournal } from "#skyblip/journal"
import { tagText } from "#skyblip/tango"
import { factsOf, shortName } from "controllers/briefing/facts"
import { say, sayNotice } from "controllers/briefing/words"

const WORKING = { asking: "words.asking_server", registering: "words.registering", unlocking: "words.registering" }
const RECONNECT = new Set(["cancelled", "refused_on_device", "claimed", "small_mtu", "link_lost"])

export default class extends Controller {
  static targets = ["name", "facts", "hint", "message", "find", "activate", "reconnect", "kept", "usePhone", "handoff", "handoffCode"]
  static values = { challenges: String, registrations: String, devices: String, handoff: String }
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

  glassOutletConnected(glass) {
    this.glass = glass
  }

  glassOutletDisconnected() {
    this.glass = null
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
    this.handoffTarget.hidden = true
    const unit = this.candidate
    if (this.#activator.state.notice?.key === "small_mtu" || unit.state?.notice?.key === "small_mtu") this.mtuRetry = unit
    await unit.reconnect()
  }

  usePhone() {
    const unit = this.candidate
    const query = new URLSearchParams({ installed: "go" })
    if (tagText(unit.tag)) query.set("tag", tagText(unit.tag).toLowerCase())
    this.handoffCodeTarget.src = `${this.handoffValue}?${query}`
    this.handoffTarget.hidden = false
    unit.disconnect()
    sharedJournal().record("installer.handed_off", { product: "go", cause: "small_mtu" }, { device: unit.slug })
    this.#render()
  }

  get #activator() {
    const token = document.querySelector('meta[name="csrf-token"]')?.content
    const server = httpServer({ challenges: this.challengesValue, registrations: this.registrationsValue, token, browser: sharedJournal().browser })
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
    const usingPhone = !this.handoffTarget.hidden
    this.hintTarget.hidden = linked || usingPhone
    this.findTarget.hidden = linked || usingPhone || !bluetoothReadiness().ready
    this.findTarget.disabled = unit?.busy ?? false
    const ready = state?.phase === "ready" && state.read && !state.address
    const notice = state?.notice ?? this.failure ?? unit?.state?.notice
    this.activateTarget.hidden = !ready || RECONNECT.has(notice?.key)
    this.keptTarget.hidden = !ready
    this.reconnectTarget.hidden = !usingPhone && !(linked && RECONNECT.has(notice?.key))
    this.usePhoneTarget.hidden = !(linked && notice?.key === "small_mtu" && this.mtuRetry === unit) || usingPhone
    this.#message(state, notice)
    this.#glass(unit, state)
  }

  #activated(unit) {
    if (this.leaving) return
    this.leaving = true
    Turbo.visit(`${this.devicesValue}/${unit.slug}?activated=1`, { action: "replace" })
  }

  #message(state, notice) {
    const readiness = bluetoothReadiness()
    const text = !readiness.ready ? (readiness.fixable ? "" : say("activation_notices.no_bluetooth"))
      : WORKING[state?.phase] ? say(WORKING[state.phase])
      : notice && notice.key !== "activated" ? sayNotice(notice, "activation_notices", "notices")
      : ""
    this.messageTarget.hidden = !text
    this.messageTarget.textContent = text
  }

  #glass(unit, state) {
    if (!this.glass) return
    if (state?.phase === "confirming") this.glass.show({ title: "ACTIVATE", prompt: "register", product: "Go" })
    else this.glass.hide()
  }
}
