import { Controller } from "@hotwired/stimulus"
import { Turbo } from "@hotwired/turbo-rails"
import { hasWebBluetooth, joinFleet } from "#skyblip/fleet"
import { battery, newsOf, seen, shortName } from "controllers/briefing/facts"
import { stayAwake } from "controllers/briefing/wake"
import { say, sayNotice } from "controllers/briefing/words"

let started = false

export default class extends Controller {
  static targets = ["list", "row", "unsupported", "first", "title", "connectNew", "activate", "notice"]
  static values = { variant: String, current: String, devices: String, activation: String }

  connect() {
    this.session = joinFleet(() => this.#render())
    this.fleet = this.session.fleet
    this.#render()
    this.fleet.restore().catch(error => console.warn("The devices this browser granted could not be listed:", error.message))
    this.#openTheOnlyOne()
  }

  disconnect() {
    this.session.leave()
  }

  async connectNew() {
    this.#say(null)
    this.connectNewTarget.disabled = true
    try {
      const unit = await this.fleet.connectNew()
      if (unit) this.#open(unit)
    } catch (error) {
      this.#say(sayNotice({ key: "failed", detail: error.message }, "notices"))
    } finally {
      this.connectNewTarget.disabled = false
    }
  }

  async link({ currentTarget }) {
    this.#say(null)
    const { slug } = currentTarget.closest("[data-slug]").dataset
    try {
      const unit = await this.fleet.connect(slug)
      if (this.variantValue === "home" && unit.linked) this.#open(unit)
    } catch (error) {
      this.#say(sayNotice({ key: "failed", detail: error.message }, "notices"))
    }
  }

  #open(unit) {
    if (unit.slug.startsWith("activate-") || unit.needsActivation) return Turbo.visit(this.activationValue)
    if (unit.state?.notice?.key === "not_skyblip") return this.#say(sayNotice(unit.state.notice, "notices"))
    if (unit.remembered) Turbo.visit(this.#path(unit))
  }

  // INFO: fc 11nov26 the app opened from its icon goes straight to the one device this browser knows, once per page load
  #openTheOnlyOne() {
    if (started) return
    started = true
    const [only, ...others] = this.fleet.remembered
    if (this.variantValue === "home" && only && others.length === 0) Turbo.visit(this.#path(only), { action: "replace" })
  }

  #render() {
    const bluetooth = hasWebBluetooth()
    const units = this.fleet.units.filter(unit => unit.remembered || unit.slug.startsWith("activate-"))
    const known = this.fleet.remembered.length > 0
    stayAwake(units.some(unit => unit.state?.phase === "uploading"))
    const rows = units.map(unit => this.#row(unit, bluetooth))
    const drawn = rows.map(row => row.outerHTML).join("")
    if (drawn !== this.drawn) {
      this.drawn = drawn
      this.listTarget.replaceChildren(...rows)
    }
    this.connectNewTarget.hidden = !bluetooth || this.currentValue === "activate"
    this.connectNewTarget.classList.toggle("button-app--quiet", known && this.variantValue === "home")
    // INFO: fc 11nov26 a browser that already knows a device mostly comes back to it, so activation moves to the menu
    this.activateTarget.hidden = !bluetooth || known || this.currentValue === "activate"
    if (this.variantValue === "home") {
      this.unsupportedTarget.hidden = bluetooth
      this.firstTarget.hidden = !bluetooth || units.length > 0
      this.titleTarget.hidden = units.length === 0
    }
  }

  #row(unit, bluetooth) {
    const row = this.rowTarget.content.firstElementChild.cloneNode(true)
    const slot = name => row.querySelector(`[data-slot="${name}"]`)
    row.dataset.slug = unit.slug
    row.classList.toggle("is-linked", unit.linked)
    row.classList.toggle("is-busy", unit.busy)
    row.classList.toggle("has-news", this.#news(unit))
    const link = slot("link")
    link.href = unit.slug.startsWith("activate-") ? this.activationValue : this.#path(unit)
    if (unit.slug === this.currentValue || (this.currentValue === "activate" && unit.slug.startsWith("activate-"))) link.setAttribute("aria-current", "page")
    slot("name").textContent = shortName(unit)
    slot("facts").textContent = this.#facts(unit)
    const news = newsOf(unit)
    slot("news").hidden = !news
    slot("attention").hidden = !this.#news(unit)
    slot("news").textContent = news ? say("words.firmware_out", { version: news }) : ""
    const connect = slot("connect")
    connect.hidden = !bluetooth || unit.linked || unit.slug.startsWith("activate-")
    connect.disabled = unit.busy
    connect.setAttribute("aria-label", `${connect.textContent.trim()} ${unit.name}`)
    return row
  }

  #facts(unit) {
    const phase = unit.state?.phase
    const callsign = unit.record.settings?.callsign || null
    const progress = unit.state?.progress
    if (phase === "uploading" && progress) return say("words.sending_short", { percent: Math.floor(100 * progress.sent / progress.total) })
    if (unit.linked) return [say("words.connected"), battery(unit.state.status)].filter(Boolean).join(" · ")
    if (phase === "connecting" || phase === "rebooting") return say(phase === "connecting" ? "words.connecting" : "words.restarting")
    const facts = this.variantValue === "home" ? [callsign, say("words.not_connected"), seen(unit.record)] : [callsign ?? say("words.not_connected")]
    return facts.filter(Boolean).join(" · ")
  }

  #news(unit) {
    const state = unit.state
    return Boolean(newsOf(unit) || state?.refusedOnDevice || state?.image?.settings || unit.needsActivation)
  }

  #path(unit) {
    return `${this.devicesValue}/${unit.slug}`
  }

  #say(text) {
    this.noticeTarget.hidden = !text
    this.noticeTarget.textContent = text ?? ""
  }
}
