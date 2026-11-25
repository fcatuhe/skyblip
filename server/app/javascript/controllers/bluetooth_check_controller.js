import { Controller } from "@hotwired/stimulus"
import { bluetoothReadiness } from "#skyblip/readiness"

export default class extends Controller {
  static targets = ["item", "linux"]
  static values = { ok: String, missing: String, unknown: String, optional: String }

  connect() {
    this.readiness = bluetoothReadiness()
    this.leave = this.readiness.listen(() => this.#render())
    this.#render()
  }

  disconnect() {
    this.leave()
  }

  #render() {
    const { checks, fixable, linux } = this.readiness
    this.element.hidden = !fixable
    for (const item of this.itemTargets) {
      const status = checks[item.dataset.check]
      item.hidden = status === null
      item.dataset.status = status ?? ""
      item.querySelector("[data-slot=status]").textContent = this.#word(status, "optional" in item.dataset)
    }
    for (const hint of this.linuxTargets) hint.hidden = !linux
  }

  #word(status, optional) {
    if (status === "missing" && optional) return this.optionalValue
    return status ? this[`${status}Value`] : ""
  }
}
