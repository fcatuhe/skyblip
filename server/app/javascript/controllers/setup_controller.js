import { Controller } from "@hotwired/stimulus"
import { Turbo } from "@hotwired/turbo-rails"
import { Activator, httpServer } from "#skyblip/activation"
import { bluetoothReadiness, installedNames, joinFleet } from "#skyblip/fleet"
import { DRIVE_NAMES, Installer } from "#skyblip/installer"
import { sharedJournal } from "#skyblip/journal"
import { browserSupport, onDesktopLinux } from "#skyblip/readiness"
import { readSetting, settingsFrame, showSetting } from "#skyblip/settings"
import { PARTNER, PairingFailed, pairTango, sides, tagText } from "#skyblip/tango"
import { storedBrowserKey } from "#skyblip/trust"
import { shortName } from "controllers/briefing/facts"
import { say, sayNotice } from "controllers/briefing/words"

const PANELS = {
  idle: "start", choosing_port: "start", reading: "start",
  drive: "drive", choosing_drive: "drive", downloading: "writing", writing: "writing",
}
const STEPS = { start: "install", drive: "install", writing: "install", connect: "connect", phone: "connect", finish: "connect", aircraft: "aircraft", done: "aircraft" }
const INSTALLED_KEY = "skyblip.setup.installed.v1"
const SETUP_NOTICES = ["setup_notices", "notices"]
const ACTIVATING = { asking: "words.asking_server", registering: "words.registering", unlocking: "words.registering" }
const PRODUCT_WORDS = { go: "Go", bravo: "Bravo" }
const FIELDS = { go: ["aircraft_type", "callsign", "aircraft_address"], tango: ["aircraft_type", "callsign", "aircraft_address"], bravo: ["aircraft_address"] }

// INFO: fc 17nov26 one page from Meshtastic to a unit ready to fly: USB, Bluetooth, then the aircraft (skyblip/README.md, The installer)
export default class extends Controller {
  static targets = ["title", "steps", "panel", "install", "driveText", "byHand", "chooseDrive", "writingText", "connectText", "connect", "connectHint",
                    "finishText", "pressBravo", "retry", "aircraftText", "aircraftForm", "aircraftFields", "transponder", "transponderSummary", "save", "editNow", "later",
                    "doneText", "open", "setUpPartner", "guides", "unsupportedText", "message", "messageText", "connectRunning", "installers",
                    "linux", "bluetoothFirst", "phoneAhead", "handoff", "handoffCode", "handoffText", "another", "copyLink", "byHandLink"]
  static values = { devices: String, setup: String, partner: String, challenges: String, registrations: String, installed: String, tag: String, handoff: String }
  static outlets = ["glass"]

  connect() {
    this.session = joinFleet(() => this.#render())
    this.fleet = this.session.fleet
    this.installer = new Installer({
      onChange: state => this.#installerChanged(state),
      serial: navigator.serial,
      pickDrive: () => window.showDirectoryPicker({ id: "skyblip-drive", mode: "readwrite" }),
      download: product => this.#download(product),
      browserKey: storedBrowserKey,
      journal: sharedJournal(),
    })
    this.resumed = this.#handedOver() ?? this.#recalled()
    if (this.resumed) sharedJournal().record("installer.resumed", { product: this.resumed.product, from: this.resumed.from })
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

  restart() {
    this.failure = null
    return this.installer.restart()
  }

  install() {
    this.failure = null
    return this.installer.install()
  }

  another() {
    this.#forget()
    return this.restart()
  }

  async connectInstalled() {
    this.failure = null
    const { product, tag } = this.#awaiting()
    try {
      const unit = await this.fleet.connectNew(installedNames(product, tag))
      if (unit) this.#onboard(unit)
    } catch (error) {
      this.failure = { key: "failed", detail: error.message }
    }
    this.#render()
  }

  async connectRunning() {
    this.failure = null
    try {
      const unit = await this.fleet.connectNew()
      if (unit?.needsActivation || unit?.slug.startsWith("activate-")) return this.#onboard(unit)
      if (unit?.remembered) return Turbo.visit(this.#path(unit.slug))
    } catch (error) {
      this.failure = { key: "failed", detail: error.message }
    }
    this.#render()
  }

  retry() {
    this.onboarding = null
    this.#onboard(this.unit)
  }

  edited() {
    this.#render()
  }

  editNow() {
    this.editing = true
    this.#render()
  }

  saveAircraft(event) {
    event.preventDefault()
    const target = this.#aircraftUnit()
    const values = this.#typed()
    if (!target || Object.keys(values).length === 0) return this.#finished()
    this.saving = true
    target.updater.saveSettings({ ...target.state.settings, ...values })
    this.#render()
  }

  skipAircraft() {
    this.#finished()
  }

  // INFO: fc 17nov26 a Go activates first, then a unit set up for a partner pairs with it, then the aircraft is named
  async #onboard(unit) {
    if (this.onboarding) return
    this.unit = unit
    this.#stage({ stage: unit.product === "go" ? "activating" : "connected", failure: null })
    if (unit.product === "go" && !(await this.#activated(unit))) return
    const partner = this.partnerValue ? this.fleet.unit(this.partnerValue) : null
    if (partner?.linked && partner.product === PARTNER[unit.product]) {
      this.#stage({ stage: "pairing", step: "arming", failure: null })
      try {
        await pairTango({ fleet: this.fleet, ...sides(unit, partner), onStep: step => this.#step(step) })
      } catch (error) {
        if (!(error instanceof PairingFailed)) throw error
        this.pairFailure = { key: error.key, detail: error.detail }
        sharedJournal().record("installer.noticed", { key: error.key, detail: error.detail ?? null, phase: "pairing" })
      }
    }
    this.#stage({ stage: "aircraft" })
  }

  // INFO: fc 26nov26 the Activator keeps the notice of an earlier attempt until it connects or asks again, so only a new notice ends this one
  #activated(unit) {
    return new Promise(resolve => {
      const activator = this.#activator(unit)
      const earlier = activator.state.notice
      let asked = false
      const stop = this.fleet.listen(() => check())
      const check = () => {
        const state = activator.state
        if (state.address) {
          stop()
          return resolve(true)
        }
        if (state.notice && state.notice !== earlier && state.notice.key !== "activated") {
          stop()
          this.#stage({ stage: "activating", failure: state.notice })
          return resolve(false)
        }
        if (state.phase === "offline") activator.connect()
        if (state.phase === "ready" && state.read && !asked) {
          asked = true
          activator.activate()
        }
      }
      check()
    })
  }

  #stage(onboarding) {
    const previous = this.onboarding?.stage ?? this.installer.state.phase
    if (onboarding.stage !== previous) {
      const now = performance.now()
      sharedJournal().record("installer.stepped", { phase: onboarding.stage, previous, lasted_ms: Math.round(now - (this.steppedAt ?? now)) })
      this.steppedAt = now
    }
    this.onboarding = onboarding
    this.#render()
  }

  #step(step) {
    this.onboarding = { ...this.onboarding, step }
    this.#render()
  }

  #activator(unit) {
    const token = document.querySelector('meta[name="csrf-token"]')?.content
    const server = httpServer({ challenges: this.challengesValue, registrations: this.registrationsValue, token, browser: sharedJournal().browser })
    return unit.activator(({ device, onChange }) => new Activator({ server, device, onChange }))
  }

  async #download(product) {
    const link = this.installersTarget.querySelector(`[data-installer="${product}"]`)
    if (!link) throw new Error(say("setup_notices.no_image"))
    const response = await fetch(link.href)
    if (!response.ok) throw new Error(`HTTP ${response.status}`)
    return { bytes: new Uint8Array(await response.arrayBuffer()), version: link.dataset.version }
  }

  #render() {
    this.#linux()
    if (this.onboarding) return this.#onboarding()
    const waiting = this.#awaiting()
    if (waiting) return this.#connectPanel(waiting)
    const support = browserSupport()
    const state = this.installer.state
    if (support !== "usb" && state.phase === "idle") return this.#elsewhere(support)
    const panel = PANELS[state.phase]
    const name = this.#name(state)
    if (panel === "start") this.#start(state)
    if (panel === "drive") this.#drive(state, name)
    if (panel === "writing") this.writingTextTarget.textContent = state.version ? say("setup.writing", { version: state.version, name }) : say("setup.downloading", { name })
    this.#show(panel, { notice: this.#onThisSystem(this.failure ?? state.notice) })
  }

  #start(state) {
    const { ready, checks } = bluetoothReadiness()
    this.installTarget.disabled = state.phase !== "idle"
    this.phoneAheadTarget.hidden = ready || checks.flag === "missing"
  }

  #elsewhere(support) {
    if (support === "phone" && bluetoothReadiness().ready) return this.#show("phone")
    this.#show("unsupported", { text: say(`setup.unsupported_${support}`) })
  }

  #drive(state, name) {
    const drive = DRIVE_NAMES[state.product] ?? `${DRIVE_NAMES.go}${say("setup.or")}${DRIVE_NAMES.bravo}`
    const words = state.drive === "restarted" ? "setup.drive" : state.drive === "ready" ? "setup.drive_ready" : state.product ? "setup.drive_gesture" : "setup.drive_by_hand"
    this.driveTextTarget.textContent = say(words, { name, drive })
    this.byHandTarget.hidden = state.drive !== "gesture"
    for (const item of this.byHandTarget.children) item.hidden = Boolean(state.product) && item.dataset.product !== state.product
    this.chooseDriveTarget.disabled = state.phase !== "drive"
  }

  #connectPanel(waiting) {
    const name = this.#name(waiting)
    const { ready, checks } = bluetoothReadiness()
    const installing = browserSupport() === "usb"
    if (!ready && !installing) return this.#show("unsupported", { text: say("setup.resumed_no_bluetooth", { name }), copy: true })
    const code = tagText(waiting.tag) ?? "…"
    const listed = waiting.product === "go" ? `activate ${code} skyBlip` : `skyBlip Bravo ${code}`
    this.connectTarget.hidden = !ready
    this.anotherTarget.hidden = !installing || (ready && waiting.from !== "tab")
    this.handoffTarget.hidden = ready
    if (ready) {
      this.connectTextTarget.textContent = say(waiting.from === "code" ? "setup.resumed" : waiting.trusted ? "setup.installed_trusted" : "setup.installed", { name })
      this.connectHintTarget.textContent = say("setup.choose_in_list", { name: listed })
    } else {
      this.connectTextTarget.textContent = say("setup.installed_no_bluetooth", { name })
      this.handoffCodeTarget.src = this.#handoffCode(waiting)
      this.handoffTextTarget.textContent = say("setup.handoff.scan", { name: listed })
      this.connectHintTarget.textContent = say(checks.flag === "missing" ? "setup.connect_after_flag" : "setup.connect_after_adapter")
      this.#handOff(waiting, checks)
    }
    this.#show("connect", { notice: this.failure })
  }

  #handoffCode({ product, tag }) {
    const query = new URLSearchParams({ installed: product })
    if (tag !== null && tag !== undefined) query.set("tag", tag.toString(16).padStart(4, "0"))
    return `${this.handoffValue}?${query}`
  }

  #handOff({ product }, checks) {
    if (this.handedOff) return
    this.handedOff = true
    const cause = checks.flag === "missing" ? "flag" : checks.adapter === "missing" ? "adapter" : "browser"
    sharedJournal().record("installer.handed_off", { product, cause })
  }

  #awaiting() {
    const state = this.installer.state
    if (state.phase === "installed") return state
    return state.phase === "idle" ? this.resumed : null
  }

  #installerChanged(state) {
    if (state.phase === "installed") {
      const { product, tag, trusted } = state
      this.#storage()?.setItem(INSTALLED_KEY, JSON.stringify({ product, tag, trusted }))
    }
    this.#render()
  }

  #handedOver() {
    if (!this.installedValue) return null
    return { product: this.installedValue, tag: this.tagValue ? parseInt(this.tagValue, 16) : null, trusted: false, from: "code" }
  }

  #recalled() {
    const stored = this.#storage()?.getItem(INSTALLED_KEY)
    return stored ? { ...JSON.parse(stored), from: "tab" } : null
  }

  #forget() {
    this.resumed = null
    this.handedOff = false
    this.#storage()?.removeItem(INSTALLED_KEY)
  }

  #storage() {
    try {
      return window.sessionStorage
    } catch (error) {
      console.warn("This browser blocks site data, so the setup forgets the unit it installed on a reload:", error.message)
      return null
    }
  }

  #onboarding() {
    const { stage, failure } = this.onboarding
    if (stage === "aircraft") return this.#aircraft()
    if (stage === "done") return this.#done()
    const unit = this.unit
    const activator = unit.product === "go" ? this.#activator(unit) : null
    const confirming = activator?.state.phase === "confirming"
    this.finishTextTarget.textContent = stage === "pairing" ? this.#pairingText() : !activator ? "" : confirming ? say("setup.confirm_activation")
      : ACTIVATING[activator.state.phase] ? say(ACTIVATING[activator.state.phase]) : say("setup.activating", { name: `Go ${tagText(unit.tag) ?? ""}`.trim() })
    if (this.glass) confirming ? this.glass.show({ title: "ACTIVATE", prompt: "register", product: "Go" }) : this.glass.hide()
    this.pressBravoTarget.hidden = stage !== "pairing" || this.onboarding.step !== "press_bravo"
    this.retryTarget.hidden = !failure
    this.#show("finish", { notice: failure, scopes: ["activation_notices", "notices"] })
  }

  #pairingText() {
    const partner = this.fleet.unit(this.partnerValue)
    const { go, bravo } = sides(this.unit, partner)
    return say(`tango.steps.${this.onboarding.step}`, { go: shortName(go), bravo: shortName(bravo) })
  }

  #case() {
    const go = this.unit.product === "go" ? this.unit : null
    const paired = this.unit.state?.pairedTag !== null && this.unit.state?.pairedTag !== undefined
    if (paired) return "tango"
    return go ? "go" : "bravo"
  }

  #aircraftUnit() {
    if (this.unit.product === "go") return this.unit
    if (this.#case() === "tango") return this.fleet.units.find(unit => unit.product === "go" && unit.linked && unit.tag === this.unit.state.pairedTag) ?? null
    return this.unit
  }

  #aircraft() {
    const kind = this.#case()
    const target = this.#aircraftUnit()
    const state = target?.state
    if (this.saving && state?.notice?.during === "set" && state.notice.key === "saved") return this.#finished()
    if (state?.settings && this.filled !== target) this.#fill(state.settings, target)
    const shown = kind === "tango" && !this.editing ? [] : FIELDS[kind]
    for (const field of this.aircraftFieldsTarget.querySelectorAll("[data-setting]")) field.hidden = !shown.includes(field.dataset.setting)
    if (kind === "tango" && this.editing) this.transponderTarget.open = true
    this.aircraftTextTarget.textContent = say(`setup.aircraft_${kind === "tango" && this.editing ? "go" : kind}`)
    this.transponderSummaryTarget.textContent = say(kind === "bravo" ? "setup.transponder_known" : "setup.transponder_go")
    const typed = Object.keys(this.#typed()).length > 0
    const working = this.saving && ["asking", "confirming"].includes(state?.phase)
    this.saveTarget.textContent = say(typed ? "setup.save" : "setup.done")
    this.saveTarget.disabled = working
    this.editNowTarget.hidden = kind !== "tango" || this.editing
    this.laterTarget.hidden = !typed || working
    this.aircraftFieldsTarget.disabled = working
    const confirming = this.saving && state?.phase === "confirming" && state.task === "set"
    if (this.glass) confirming ? this.glass.show({ title: "SETTINGS", prompt: "set", product: PRODUCT_WORDS[target.product], lamp: target.product === "bravo",
      argument: JSON.stringify(state.asked), settings: JSON.stringify({ command: "set", ...settingsFrame(state.settings ?? {}) }) }) : this.glass.hide()
    const saveNotice = this.saving && state?.notice?.during === "set" && state.notice.key !== "saved" ? state.notice : null
    this.#show("aircraft", { notice: saveNotice ?? this.pairFailure, scopes: saveNotice ? ["set_notices", "notices"] : ["pair_notices", "notices"] })
  }

  #fill(settings, target) {
    this.filled = target
    for (const field of this.aircraftFieldsTarget.querySelectorAll("[name]")) {
      if (field.name in settings) showSetting(field, settings[field.name])
    }
  }

  #typed() {
    const stored = this.#aircraftUnit()?.state?.settings ?? {}
    const typed = {}
    for (const field of this.aircraftFieldsTarget.querySelectorAll("[data-setting]:not([hidden]) [name]")) {
      const value = readSetting(field)
      if (field.name in stored && value !== stored[field.name]) typed[field.name] = value
    }
    return typed
  }

  #finished() {
    sharedJournal().record("installer.finished", { kind: this.#case(), saved: Boolean(this.saving) })
    this.#forget()
    this.saving = false
    this.#stage({ stage: "done" })
  }

  #done() {
    if (this.glass) this.glass.hide()
    const kind = this.#case()
    const shown = this.#aircraftUnit() ?? this.unit
    const partner = kind === "tango" ? this.fleet.units.find(unit => unit !== shown && unit.linked && unit.product === PARTNER[shown.product]) : null
    this.doneTextTarget.textContent = say(`setup.done_${kind}`, { name: shortName(shown), partner: partner ? shortName(partner) : "" })
    this.openTarget.href = this.#path(shown.slug)
    this.openTarget.textContent = say("setup.open", { name: shortName(shown) })
    const other = PARTNER[this.unit.product]
    const known = this.fleet.units.some(unit => unit.product === other && (unit.remembered || unit.linked))
    this.setUpPartnerTarget.hidden = known
    this.setUpPartnerTarget.href = `${this.setupValue}?for=${this.unit.slug}`
    this.setUpPartnerTarget.textContent = say("tango.set_up", { other: PRODUCT_WORDS[other] })
    for (const item of this.guidesTarget.children) item.hidden = kind !== "tango" && item.dataset.product !== kind
    this.#show("done")
  }

  #linux() {
    const linux = onDesktopLinux(navigator)
    for (const each of this.linuxTargets) each.hidden = !linux
    this.bluetoothFirstTarget.hidden = bluetoothReadiness().checks.flag !== "missing"
  }

  #onThisSystem(notice) {
    return notice?.key === "port_busy" && onDesktopLinux(navigator) ? { ...notice, key: "port_busy_linux" } : notice
  }

  #name({ product, tag }) {
    if (!product) return say("setup.the_device")
    return [PRODUCT_WORDS[product], tagText(tag)].filter(Boolean).join(" ")
  }

  #show(panel, { text = null, notice = null, scopes = SETUP_NOTICES, copy = false } = {}) {
    for (const each of this.panelTargets) each.hidden = each.dataset.panel !== panel
    if (panel === "unsupported") this.unsupportedTextTarget.textContent = text
    this.copyLinkTarget.hidden = !copy
    this.byHandLinkTarget.hidden = copy
    const step = STEPS[panel]
    for (const item of this.stepsTarget.children) {
      if (item.dataset.step === step) item.setAttribute("aria-current", "step")
      else item.removeAttribute("aria-current")
    }
    this.stepsTarget.hidden = panel === "unsupported"
    const message = notice ? sayNotice(notice, ...scopes) : ""
    const running = (panel === "unsupported" || notice?.key === "port_closed") && bluetoothReadiness().ready
    this.messageTarget.hidden = !message && !running
    this.messageTextTarget.hidden = !message
    this.messageTextTarget.textContent = message
    this.connectRunningTarget.hidden = !running
  }

  #path(slug) {
    return `${this.devicesValue}/${slug}`
  }
}
