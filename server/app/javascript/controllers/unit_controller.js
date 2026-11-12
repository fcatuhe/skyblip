import { Controller } from "@hotwired/stimulus"
import { Turbo } from "@hotwired/turbo-rails"
import { hasWebBluetooth, joinFleet, nameOf } from "#skyblip/fleet"
import { readSetting, showSetting } from "#skyblip/settings"
import { factsOf, newsOf, productWord, running, trackOf } from "controllers/briefing/facts"
import { offerNewest, shelfIn } from "controllers/briefing/shelf"
import { say, sayCount, sayNotice } from "controllers/briefing/words"

const SET_NOTICES = ["set_notices", "notices"]
const CAPTURE_NOTICES = ["capture_notices", "notices"]
const CAPTURE_DONE = new Set(["capture_armed", "capture_stopped"])
const PROMPTS = { set: "SETTINGS", capture: "CAPTURE" }
const LINK_NOTICES = new Set(["link_lost", "not_back", "timeout", "claimed", "small_mtu", "no_bluetooth", "not_skyblip", "failed", "no_params"])
const REFUSALS = new Set(["cancelled", "refused_on_device"])
const WORKING = new Set(["asking", "confirming", "uploading", "installing", "rebooting"])
const LAST_READ = ["aircraft_type", "callsign", "own_icao", "units", "alarm_enabled", "alarm_volume"]

export default class extends Controller {
  static targets = ["name", "facts", "menu", "disconnect", "notes", "banner", "bannerText", "bannerAction", "offline", "lastRead", "connect", "reconnect",
                    "settings", "fields", "default", "save", "reset", "settingsMessage", "firmwareRow", "firmware",
                    "capture", "captureFields", "captureProfile", "captureArm", "captureStop", "captureMessage",
                    "forgetDialog", "forgetQuestion"]
  static values = { slug: String, home: String, activation: String }
  static outlets = ["glass"]

  connect() {
    this.session = joinFleet(() => this.#render())
    this.fleet = this.session.fleet
    this.framed = () => this.#render()
    this.element.addEventListener("turbo:frame-load", this.framed)
    this.#render()
  }

  disconnect() {
    this.element.removeEventListener("turbo:frame-load", this.framed)
    this.session.leave()
  }

  get unit() {
    return this.fleet.unit(this.slugValue)
  }

  async link() {
    this.failure = null
    try {
      await this.fleet.connect(this.slugValue)
    } catch (error) {
      this.failure = { key: "failed", detail: error.message }
    }
    this.#render()
  }

  async reconnect() {
    const unit = this.unit
    if (unit.linked) {
      const dropped = new Promise(resolve => {
        const stop = this.fleet.listen(() => {
          if (!unit.linked) resolve(stop())
        })
      })
      unit.disconnect()
      await dropped
    }
    return this.link()
  }

  unlink() {
    this.menuTarget.open = false
    this.unit?.disconnect()
  }

  askToForget() {
    this.menuTarget.open = false
    this.forgetQuestionTarget.textContent = say("words.forget_question", { name: this.unit.name })
    this.forgetDialogTarget.showModal()
  }

  forget() {
    this.fleet.forget(this.slugValue)
    Turbo.visit(this.homeValue)
  }

  edit() {
    this.#settings(this.unit.state)
  }

  save(event) {
    event.preventDefault()
    const edited = this.#edited()
    const changed = Object.keys(edited).some(key => edited[key] !== this.unit.state.settings[key])
    if (!changed && this.#activated()) return Turbo.visit(location.pathname, { action: "replace" })
    this.unit.updater.saveSettings(edited)
  }

  armCapture(event) {
    event.preventDefault()
    this.unit.updater.orderCapture(this.captureProfileTarget.value)
  }

  stopCapture() {
    this.unit.updater.orderCapture("stop")
  }

  #activated() {
    return new URLSearchParams(location.search).has("activated")
  }

  // INFO: fc 11nov26 the defaults fill the form, and Save sends them, so the pilot sees what will change before the glass asks
  reset() {
    const { defaults } = this.unit.state
    const { elements } = this.settingsTarget
    for (const [key, value] of Object.entries(defaults)) {
      if (elements[key] && !elements[key].disabled) showSetting(elements[key], value)
    }
    this.#settings(this.unit.state)
  }

  #render() {
    const unit = this.unit
    const state = unit?.state ?? null
    const linked = unit?.linked ?? false
    const page = this.hasSettingsTarget
    const working = WORKING.has(state?.phase)
    if (page) this.nameTarget.textContent = unit?.name ?? nameOf(this.slugValue)
    this.#facts(unit, state)
    const settling = !page && state?.image?.state === "probation"
    this.menuTarget.hidden = !unit || settling || (working && !(page && PROMPTS[state.task]))
    this.disconnectTarget.hidden = !linked
    this.reconnectTargets.forEach(button => (button.hidden = !state?.refusedOnDevice))
    this.#offline(unit, linked || state?.phase === "rebooting")
    const activated = page && linked && this.#activated()
    this.#notes(unit, state, page, activated)
    if (!page) return
    const asking = state?.phase === "confirming" && Boolean(PROMPTS[state.task])
    const quiet = asking || activated || state?.refusedOnDevice || state?.status?.flight === "airborne"
    this.#banner(unit, linked, quiet)
    this.#settings(state)
    this.#firmwareRow(state, linked && !asking)
    this.#capture(unit, state)
    this.#glass(unit, asking)
    if (linked) offerNewest(unit, shelfIn(this.element))?.then(() => this.#render())
  }

  #facts(unit, state) {
    const facts = unit ? factsOf(unit) : [say("words.not_met")]
    const airborne = state?.status?.flight === "airborne" ? say("words.flight.airborne") : null
    this.factsTarget.replaceChildren(...facts.flatMap((fact, index) => {
      const item = Object.assign(document.createElement("span"), { textContent: fact })
      if (fact === airborne) item.className = "is-airborne"
      return index === 0 ? [item] : [" · ", item]
    }))
  }

  #offline(unit, away) {
    this.offlineTarget.hidden = away
    const busy = unit?.busy ?? false
    this.connectTarget.hidden = !hasWebBluetooth()
    this.connectTarget.disabled = busy
    this.connectTarget.textContent = busy ? say("words.connecting") : this.connectTarget.dataset.label
    if (this.hasLastReadTarget) this.#lastRead(unit, away)
  }

  #lastRead(unit, away) {
    const record = unit?.record ?? {}
    const settings = record.settings ?? {}
    const rows = [
      ...LAST_READ.filter(key => key in settings && settings[key] !== "")
        .map(key => [this.#label(key), this.#shown(key, settings[key])]),
      [say("words.firmware"), running(record.running, record.track)],
    ].filter(([, value]) => value)
    this.lastReadTarget.hidden = away || rows.length === 0
    this.lastReadTarget.replaceChildren(...rows.map(([term, value]) => {
      const row = document.createElement("div")
      const dt = document.createElement("dt")
      const dd = document.createElement("dd")
      dt.textContent = term
      dd.textContent = value
      row.append(dt, dd)
      return row
    }))
  }

  #notes(unit, state, page, activated) {
    const notes = []
    if (activated) notes.push([say("words.activated", { name: unit.name }), null, "is-good"])
    if (unit?.needsActivation) notes.push([say("notices.not_activated"), "activate"])
    if (state?.status?.flight === "airborne") notes.push([say("words.in_flight_lock")])
    if (state?.refusedOnDevice) notes.push([say("words.reconnect_needed")])
    if (state?.image?.settings) notes.push([say(`notices.settings_${state.image.settings}`)])
    const notice = state?.notice
    if (page && notice && notice.during !== "set" && LINK_NOTICES.has(notice.key) && !state?.refusedOnDevice) notes.push([sayNotice(notice, "notices"), unit.linked ? "reconnect" : null])
    if (this.failure) notes.push([sayNotice(this.failure, "notices")])
    const shown = notes.filter(([text]) => text)
    const drawn = JSON.stringify(shown)
    if (drawn === this.drawnNotes) return
    this.drawnNotes = drawn
    this.notesTarget.replaceChildren(...shown.map(note => this.#note(...note)))
  }

  #note(text, action, tone = null) {
    const note = document.createElement("p")
    note.textContent = text
    if (tone) note.className = tone
    if (action === "activate") {
      const link = document.createElement("a")
      link.className = "button-app button-app--small"
      link.href = this.activationValue
      link.textContent = say("words.activate_now")
      note.append(" ", link)
    }
    if (action === "reconnect") {
      const button = document.createElement("button")
      button.type = "button"
      button.className = "button-app button-app--small"
      button.textContent = say("words.reconnect")
      button.dataset.action = "unit#reconnect"
      note.append(" ", button)
    }
    return note
  }

  // INFO: fc 11nov26 offline, the banner says what this browser last saw offered, and connecting is the way to it
  #banner(unit, linked, quiet) {
    const news = unit ? newsOf(unit) : null
    const shown = Boolean(news) && !quiet
    this.bannerTarget.hidden = !shown
    this.bannerActionTarget.hidden = !linked
    if (shown) this.bannerTextTarget.textContent = say("words.update_banner", { version: news, running: unit.state?.running ?? unit.record.running })
  }

  #firmwareRow(state, linked) {
    this.firmwareRowTarget.hidden = !linked
    if (!linked) return
    const version = running(state.running, trackOf(state.image))
    const news = state.upgrade === true ? say("words.firmware_out", { version: state.file.version }) : state.upgrade === false ? say("words.firmware_current") : null
    this.firmwareTarget.textContent = [version, news].filter(Boolean).join(" · ")
  }

  #glass(unit, asking) {
    if (!this.hasGlassOutlet) return
    if (asking) this.glassOutlet.show({ title: PROMPTS[unit.state.task], product: productWord(unit), lamp: unit.product === "skyblip_bravo" })
    else this.glassOutlet.hide()
  }

  #settings(state) {
    const settings = state?.settings
    const linked = this.unit?.linked ?? false
    this.settingsTarget.hidden = !linked || !settings || this.#prompting(state)
    const notice = state?.notice?.during === "set" && !this.#refusedHere(state) ? sayNotice(state.notice, ...SET_NOTICES) : ""
    this.settingsMessageTarget.hidden = !notice
    this.settingsMessageTarget.textContent = notice
    this.settingsMessageTarget.classList.toggle("is-good", state?.notice?.key === "saved")
    if (!linked || !settings) return
    if (settings !== this.shown) this.#fill(settings)
    const edited = this.#edited()
    const changed = Object.keys(edited).filter(key => edited[key] !== settings[key])
    const airborne = state.status?.flight === "airborne"
    const idle = state.phase === "ready" && !airborne && !state.refusedOnDevice
    this.fieldsTarget.disabled = !idle
    for (const field of this.settingsTarget.querySelectorAll(".field")) {
      field.classList.toggle("is-edited", changed.includes(field.querySelector("[name]")?.name))
    }
    const done = this.#activated() && changed.length === 0
    this.saveTarget.hidden = state.refusedOnDevice
    this.saveTarget.disabled = !done && (!idle || changed.length === 0)
    this.saveTarget.textContent = airborne ? say("words.locked") : done ? say("words.done_setting_up") : changed.length === 0 ? this.saveTarget.dataset.label : sayCount("words.save_count", changed.length)
    const { defaults } = state
    const differs = defaults ? Object.keys(defaults).filter(key => key in edited && defaults[key] !== edited[key]) : []
    this.resetTarget.hidden = differs.length === 0 || airborne || state.refusedOnDevice
    this.resetTarget.disabled = !idle
    this.resetTarget.textContent = say("words.reset_count", { count: differs.length })
    for (const note of this.defaultTargets) this.#default(note, edited, defaults)
  }

  #prompting(state) {
    return ["asking", "confirming"].includes(state?.phase) && Boolean(PROMPTS[state.task])
  }

  // INFO: fc 12nov26 only a Bravo arms a capture over the link, since a Go arms one on its glass
  #capture(unit, state) {
    const linked = unit?.linked ?? false
    this.captureTarget.hidden = !linked || unit.product !== "skyblip_bravo" || this.#prompting(state)
    const notice = state?.notice?.during === "capture" && !this.#refusedHere(state) && !LINK_NOTICES.has(state.notice.key) ? sayNotice(state.notice, ...CAPTURE_NOTICES) : ""
    this.captureMessageTarget.hidden = !notice
    this.captureMessageTarget.textContent = notice
    this.captureMessageTarget.classList.toggle("is-good", CAPTURE_DONE.has(state?.notice?.key))
    if (this.captureTarget.hidden) return
    const airborne = state.status?.flight === "airborne"
    const idle = state.phase === "ready" && !airborne && !state.refusedOnDevice
    this.captureFieldsTarget.disabled = !idle
    this.captureArmTarget.disabled = !idle
    this.captureArmTarget.hidden = state.refusedOnDevice
    this.captureArmTarget.textContent = airborne ? say("words.locked") : this.captureArmTarget.dataset.label
    this.captureStopTarget.disabled = !idle
    this.captureStopTarget.hidden = airborne || state.refusedOnDevice
  }

  #refusedHere({ notice, refusedOnDevice }) {
    return refusedOnDevice && REFUSALS.has(notice?.key)
  }

  #fill(settings) {
    this.shown = settings
    const { elements } = this.settingsTarget
    for (const field of this.settingsTarget.querySelectorAll(".field")) {
      const input = field.querySelector("[name]")
      const held = Boolean(input) && input.name in settings
      field.hidden = !held
      if (input) input.disabled = !held
    }
    for (const [key, value] of Object.entries(settings)) {
      if (elements[key]) showSetting(elements[key], value)
    }
  }

  #edited() {
    const { elements } = this.settingsTarget
    return Object.fromEntries(Object.keys(this.shown ?? {}).filter(key => elements[key]).map(key => [key, readSetting(elements[key])]))
  }

  #default(note, edited, defaults) {
    const key = note.dataset.setting
    const differs = Boolean(defaults) && key in defaults && key in edited && defaults[key] !== edited[key]
    note.hidden = !differs
    note.textContent = differs ? say("words.default", { value: this.#shown(key, defaults[key]) }) : ""
  }

  #shown(key, value) {
    const field = this.settingsTarget.elements[key]
    if (field.type === "checkbox") return say(value ? "words.on" : "words.off")
    if (field.tagName === "SELECT") return this.#optionText(key, value)
    if (value === "") return say("words.none")
    return String(value)
  }

  #label(key) {
    return this.settingsTarget.querySelector(`label[for="${this.settingsTarget.elements[key]?.id}"]`)?.textContent.trim()
  }

  #optionText(key, value) {
    const field = this.settingsTarget.elements[key]
    return [...field.options].find(option => option.value === String(value))?.text ?? String(value)
  }
}
