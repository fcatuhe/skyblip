import { Controller } from "@hotwired/stimulus"
import { Turbo } from "@hotwired/turbo-rails"
import { Activator, httpServer } from "#skyblip/activation"
import { hasWebBluetooth, installedNames, joinFleet } from "#skyblip/fleet"
import { DRIVE_NAMES, Installer, browserSupport } from "#skyblip/installer"
import { sharedJournal } from "#skyblip/journal"
import { tagText } from "#skyblip/tango"
import { storedBrowserKey } from "#skyblip/trust"
import { say, sayNotice } from "controllers/briefing/words"

const PANELS = {
  idle: "start", choosing_port: "start", reading: "start",
  drive: "drive", choosing_drive: "drive", downloading: "writing", writing: "writing",
  installed: "connect",
}
const STEPS = { start: "install", drive: "install", writing: "install", connect: "connect", activate: "finish" }
const SETUP_NOTICES = ["setup_notices", "notices"]
const ACTIVATION_NOTICES = ["activation_notices", "notices"]
const ACTIVATING = { asking: "words.asking_server", registering: "words.registering", unlocking: "words.registering" }
const PRODUCT_WORDS = { go: "Go", bravo: "Bravo" }

// INFO: fc 17nov26 one page from Meshtastic to a connected skyBlip: USB first, then Bluetooth (skyblip/README.md, The installer)
export default class extends Controller {
  static targets = ["title", "steps", "panel", "install", "driveText", "byHand", "chooseDrive", "writingText", "connectText", "connect", "connectHint",
                    "activateText", "retry", "unsupportedText", "bluetoothOnly", "message", "installers"]
  static values = { devices: String, activation: String, partner: String, challenges: String, registrations: String }
  static outlets = ["glass"]

  connect() {
    this.session = joinFleet(() => this.#render())
    this.fleet = this.session.fleet
    this.installer = new Installer({
      onChange: () => this.#render(),
      serial: navigator.serial,
      pickDrive: () => window.showDirectoryPicker({ id: "skyblip-drive", mode: "readwrite" }),
      download: product => this.#download(product),
      browserKey: storedBrowserKey,
    })
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

  skipToDrive() {
    this.failure = null
    this.installer.skipToDrive()
  }

  install() {
    this.failure = null
    return this.installer.install()
  }

  async connectInstalled() {
    this.failure = null
    const { product, tag } = this.installer.state
    try {
      this.unit = await this.fleet.connectNew(installedNames(product, tag))
    } catch (error) {
      this.failure = { key: "failed", detail: error.message }
    }
    this.#render()
  }

  activate() {
    this.#activator.activate()
  }

  async connectRunning() {
    this.failure = null
    try {
      const unit = await this.fleet.connectNew()
      if (unit?.needsActivation || unit?.slug.startsWith("activate-")) return Turbo.visit(this.activationValue)
      if (unit?.remembered) return Turbo.visit(this.#path(unit.slug))
    } catch (error) {
      this.failure = { key: "failed", detail: error.message }
    }
    this.#render()
  }

  async #download(product) {
    const link = this.installersTarget.querySelector(`[data-installer="${product}"]`)
    if (!link) throw new Error(say("setup_notices.no_image"))
    const response = await fetch(link.href)
    if (!response.ok) throw new Error(`HTTP ${response.status}`)
    return { bytes: new Uint8Array(await response.arrayBuffer()), version: link.dataset.version }
  }

  get #activator() {
    const token = document.querySelector('meta[name="csrf-token"]')?.content
    const server = httpServer({ challenges: this.challengesValue, registrations: this.registrationsValue, token, browser: sharedJournal().browser })
    return this.unit.activator(({ device, onChange }) => new Activator({ server, device, onChange }))
  }

  #render() {
    const support = browserSupport()
    if (support !== "usb" && this.installer.state.phase === "idle") return this.#show("unsupported", { text: say(`setup.unsupported_${support}`) })
    if (this.unit?.linked) return this.#connected(this.unit)
    const state = this.installer.state
    const panel = PANELS[state.phase]
    const name = this.#name(state)
    if (panel === "start") this.installTarget.disabled = state.phase !== "idle"
    if (panel === "drive") this.#drive(state, name)
    if (panel === "writing") this.writingTextTarget.textContent = state.version ? say("setup.writing", { version: state.version, name }) : say("setup.downloading", { name })
    if (panel === "connect") this.#connectPanel(state, name)
    const notice = this.failure ?? (state.notice?.key === "by_hand" ? null : state.notice)
    this.#show(panel, { notice })
  }

  #drive(state, name) {
    const drive = DRIVE_NAMES[state.product] ?? `${DRIVE_NAMES.go}${say("setup.or")}${DRIVE_NAMES.bravo}`
    const byHand = ["no_meshtastic", "by_hand"].includes(state.notice?.key) || !state.product
    this.driveTextTarget.textContent = byHand ? say("setup.drive_by_hand", { drive }) : say("setup.drive", { name, drive })
    this.byHandTarget.hidden = !byHand
    for (const item of this.byHandTarget.children) item.hidden = Boolean(state.product) && item.dataset.product !== state.product
    this.chooseDriveTarget.disabled = state.phase !== "drive"
  }

  #connectPanel(state, name) {
    this.connectTextTarget.textContent = say(state.trusted ? "setup.installed_trusted" : "setup.installed", { name })
    const shown = state.product === "go" ? (state.tag === null ? "activate … skyBlip" : `activate ${tagText(state.tag)} skyBlip`)
      : state.tag === null ? "skyBlip Bravo …" : `skyBlip Bravo ${tagText(state.tag)}`
    this.connectHintTarget.textContent = say("setup.choose_in_list", { name: shown })
    this.connectTarget.hidden = !hasWebBluetooth()
  }

  #connected(unit) {
    if (unit.product === "bravo") return this.#leave(this.#path(unit.slug), { installed: 1 })
    const activator = this.#activator
    const state = activator.state
    if (state.address) return this.#leave(this.#path(`go-${state.address.toLowerCase()}`), { activated: 1 })
    if (state.phase === "offline" && !state.notice) activator.connect()
    if (state.phase === "ready" && state.read && !state.notice && !this.activating) {
      this.activating = true
      activator.activate()
    }
    const name = `Go ${tagText(unit.tag) ?? ""}`.trim()
    const working = ACTIVATING[state.phase]
    this.activateTextTarget.textContent = state.phase === "confirming" ? say("setup.confirm_activation") : working ? say(working) : say("setup.activating", { name })
    if (this.glass) state.phase === "confirming" ? this.glass.show({ title: "ACTIVATE", prompt: "register", product: "Go" }) : this.glass.hide()
    const notice = state.notice && state.notice.key !== "activated" ? state.notice : null
    this.retryTarget.hidden = !(notice && state.phase === "ready")
    this.#show("activate", { notice, scopes: ACTIVATION_NOTICES })
  }

  #leave(path, params) {
    if (this.leaving) return
    this.leaving = true
    const query = new URLSearchParams({ ...params, ...(this.partnerValue ? { pair: this.partnerValue } : {}) })
    Turbo.visit(`${path}?${query}`, { action: "replace" })
  }

  #name({ product, tag }) {
    if (!product) return say("setup.the_device")
    return [PRODUCT_WORDS[product], tagText(tag)].filter(Boolean).join(" ")
  }

  #show(panel, { text = null, notice = null, scopes = SETUP_NOTICES } = {}) {
    for (const each of this.panelTargets) each.hidden = each.dataset.panel !== panel
    if (panel === "unsupported") {
      this.unsupportedTextTarget.textContent = text
      this.bluetoothOnlyTarget.hidden = !hasWebBluetooth()
    }
    const step = STEPS[panel]
    for (const item of this.stepsTarget.children) {
      if (item.dataset.step === step) item.setAttribute("aria-current", "step")
      else item.removeAttribute("aria-current")
    }
    this.stepsTarget.hidden = panel === "unsupported"
    const message = notice ? sayNotice(notice, ...scopes) : ""
    this.messageTarget.hidden = !message
    this.messageTarget.textContent = message
  }

  #path(slug) {
    return `${this.devicesValue}/${slug}`
  }
}
