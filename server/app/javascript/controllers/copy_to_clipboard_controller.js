import { Controller } from "@hotwired/stimulus"

export default class extends Controller {
  static values = { content: String }
  static classes = [ "success" ]

  async copy(event) {
    event.preventDefault()
    this.reset()

    try {
      await navigator.clipboard.writeText(this.contentValue)
      this.element.classList.add(this.successClass)
    } catch {
      this.#selectForManualCopy()
    }
  }

  reset() {
    this.element.classList.remove(this.successClass)
    this.#forceReflow()
  }

  #selectForManualCopy() {
    window.getSelection().selectAllChildren(this.element.querySelector("code") ?? this.element)
  }

  #forceReflow() {
    this.element.offsetWidth
  }
}
