import { Controller } from "@hotwired/stimulus"
import { PROMPT_CEILING_MS } from "#skyblip/activation"
import { say } from "controllers/briefing/words"

const TICK_MS = 250

// INFO: fc 14nov26 the glass is the Go's own confirm page, drawn by the firmware compiled to WASM
export default class extends Controller {
  static targets = ["glass", "words", "lamp", "press", "seconds", "remembers"]
  static values = { module: String }

  disconnect() {
    this.#stop()
  }

  // INFO: fc 11nov26 the countdown starts when the device says it asks, as the glass's own does
  show({ title, prompt, argument = "", settings = "", product, lamp = false, remembers = false }) {
    const asking = `${prompt}|${argument}|${settings}|${product}|${remembers}`
    if (this.asking === asking) return
    this.asking = asking
    this.request = { prompt, argument, settings }
    this.lamp = lamp
    this.element.hidden = false
    this.remembersTarget.hidden = !remembers
    this.lampTarget.hidden = !lamp
    this.glassTarget.setAttribute("aria-label", title)
    this.wordsTarget.textContent = title
    this.#showGlass(false)
    this.pressTarget.textContent = say("words.glass_press", { product })
    this.askedAt = performance.now()
    this.painted = null
    clearInterval(this.ticker)
    this.ticker = setInterval(() => this.#tick(), TICK_MS)
    this.#tick()
  }

  hide() {
    this.asking = null
    this.element.hidden = true
    this.#stop()
  }

  #tick() {
    const seconds = Math.max(0, Math.ceil((PROMPT_CEILING_MS - (performance.now() - this.askedAt)) / 1000))
    this.secondsTarget.textContent = seconds > 0 ? seconds : ""
    this.secondsTarget.classList.toggle("is-short", seconds <= 10)
    if (seconds === 0) this.pressTarget.textContent = say("words.glass_waiting")
    if (seconds !== this.painted) this.#paint(seconds)
  }

  async #paint(seconds) {
    this.painted = seconds
    if (this.lamp) return
    const asking = this.asking
    const simulator = await this.#simulator()
    if (asking !== this.asking) return
    const words = simulator?.paintPrompt?.(this.glassTarget, { ...this.request, seconds }) || null
    if (words) this.glassTarget.setAttribute("aria-label", words)
    this.#showGlass(Boolean(words))
  }

  #showGlass(drawn) {
    this.glassTarget.hidden = this.lamp || !drawn
    this.wordsTarget.hidden = this.lamp || drawn
  }

  #simulator() {
    this.loading ??= import(this.moduleValue).then(({ load }) => load()).catch(() => null)
    return this.loading
  }

  #stop() {
    clearInterval(this.ticker)
  }
}
