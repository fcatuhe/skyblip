import { Controller } from "@hotwired/stimulus"
import { joinFleet } from "#skyblip/fleet"
import { PARTNER, PairingFailed, pairTango, sides, tagText, tangoView } from "#skyblip/tango"
import { shortName } from "controllers/briefing/facts"
import { say, sayNotice } from "controllers/briefing/words"

const PRODUCT_WORDS = { go: "Go", bravo: "Bravo" }
const PAIR_NOTICES = ["pair_notices", "notices"]

// INFO: fc 17nov26 the device page's offer to pair a Go with a Bravo, from either side (skyblip/README.md, Pairing from the briefing)
export default class extends Controller {
  static targets = ["title", "text", "pressBravo", "actions", "pair", "connect", "setup", "connectNew", "message"]
  static values = { slug: String, setup: String }

  connect() {
    this.session = joinFleet(() => this.#render())
    this.fleet = this.session.fleet
    this.#render()
  }

  disconnect() {
    this.session.leave()
  }

  get unit() {
    return this.fleet.unit(this.slugValue)
  }

  async pair() {
    if (this.pairing && !this.pairing.failure && this.pairing.step !== "paired") return
    const view = tangoView(this.unit, this.fleet.units)
    const partner = view?.partner?.linked ? view.partner : this.pairing?.partner
    if (!partner?.linked) return
    this.pairing = { step: "arming", partner }
    this.#render()
    try {
      await pairTango({ fleet: this.fleet, ...sides(this.unit, partner), onStep: step => this.#step(step) })
    } catch (error) {
      if (!(error instanceof PairingFailed)) throw error
      this.pairing = { ...this.pairing, failure: { key: error.key, detail: error.detail } }
    }
    this.#render()
  }

  async connectPartner() {
    const view = tangoView(this.unit, this.fleet.units)
    if (!view?.partner) return
    try {
      await this.fleet.connect(view.partner.slug)
    } catch (error) {
      this.failure = { key: "failed", detail: error.message }
    }
    this.#render()
  }

  async connectNew() {
    const product = PARTNER[this.unit.product]
    try {
      await this.fleet.connectNew({ namePrefix: product === "bravo" ? "skyBlip Bravo" : "skyBlip Go" })
    } catch (error) {
      this.failure = { key: "failed", detail: error.message }
    }
    this.#render()
  }

  #step(step) {
    if (this.pairing.step === step) return
    this.pairing = { ...this.pairing, step }
    this.#render()
  }

  #render() {
    const unit = this.unit
    const view = unit ? tangoView(unit, this.fleet.units) : null
    const pairing = this.pairing
    if (!view && !pairing) return (this.element.hidden = true)
    this.element.hidden = false
    const product = unit.product
    const other = PARTNER[product]
    this.titleTarget.textContent = say(`tango.title_${product}`)
    for (const target of [this.pairTarget, this.connectTarget, this.setupTarget, this.connectNewTarget]) target.hidden = true
    this.pressBravoTarget.hidden = true
    if (pairing) this.#pairing(unit, pairing)
    else this.#offer(unit, view, other)
    const notice = pairing?.failure ? sayNotice(pairing.failure, ...PAIR_NOTICES) : this.failure ? sayNotice(this.failure, "notices") : ""
    this.messageTarget.hidden = !notice
    this.messageTarget.textContent = notice
  }

  #offer(unit, view, other) {
    const values = { unit: shortName(unit), kind: PRODUCT_WORDS[unit.product], partner: view.partner ? shortName(view.partner) : null, other: PRODUCT_WORDS[other] }
    if (view.kind === "paired") {
      this.textTarget.textContent = say("tango.paired", { partner: view.partner ? shortName(view.partner) : `${PRODUCT_WORDS[other]} ${tagText(view.pairedTag)}` })
      return
    }
    if (view.kind === "replace") {
      this.textTarget.textContent = say("tango.replace", { ...values, current: `${PRODUCT_WORDS[other]} ${tagText(view.pairedTag)}` })
      return this.#button(this.pairTarget, say("tango.pair_instead", values))
    }
    if (view.kind === "pair") {
      const leaves = view.replaces === null ? "" : ` ${say(`tango.leaves_${other}`, values)}`
      this.textTarget.textContent = say(`tango.offer_${unit.product}`, values) + leaves
      return this.#button(this.pairTarget, say("tango.pair", values))
    }
    if (view.kind === "connect") {
      this.textTarget.textContent = say("tango.away", values)
      return this.#button(this.connectTarget, say("tango.connect", values))
    }
    this.textTarget.textContent = say(`tango.none_${unit.product}`)
    this.setupTarget.href = `${this.setupValue}?for=${unit.slug}`
    this.#button(this.setupTarget, say("tango.set_up", values))
    this.#button(this.connectNewTarget, say("tango.connect_running", values))
  }

  #pairing(unit, { step, partner, failure }) {
    const { go, bravo } = sides(unit, partner)
    const values = { go: shortName(go), bravo: shortName(bravo) }
    this.textTarget.textContent = say(failure ? "tango.not_paired" : `tango.steps.${step}`, values)
    this.pressBravoTarget.hidden = Boolean(failure) || step !== "press_bravo"
    if (failure && partner.linked) this.#button(this.pairTarget, say("tango.try_again"))
  }

  #button(target, label) {
    target.hidden = false
    target.textContent = label
  }
}
