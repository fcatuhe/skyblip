import { Controller } from "@hotwired/stimulus"
import { PROMPT_CEILING_MS } from "#skyblip/activation"
import { say } from "controllers/briefing/words"

const TICK_MS = 250

export default class extends Controller {
  static targets = ["glass", "title", "detail", "left", "lamp", "press", "seconds"]

  disconnect() {
    this.#stop()
  }

  // INFO: fc 11nov26 the countdown starts when the device says it asks, as the glass's own does
  show({ title, detail = "", product, lamp = false }) {
    const asking = `${title}|${detail}|${product}`
    if (this.asking === asking) return
    this.asking = asking
    this.element.hidden = false
    this.glassTarget.hidden = lamp
    this.lampTarget.hidden = !lamp
    this.titleTarget.textContent = title
    this.detailTarget.textContent = detail
    this.pressTarget.textContent = say("words.glass_press", { product })
    this.askedAt = performance.now()
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
    this.leftTarget.textContent = say("words.glass_left", { seconds })
    this.secondsTarget.textContent = seconds > 0 ? seconds : ""
    this.secondsTarget.classList.toggle("is-short", seconds <= 10)
    if (seconds === 0) this.pressTarget.textContent = say("words.glass_waiting")
  }

  #stop() {
    clearInterval(this.ticker)
  }
}
