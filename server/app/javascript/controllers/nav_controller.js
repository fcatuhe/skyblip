import { Controller } from "@hotwired/stimulus"

export default class extends Controller {
  static targets = [ "panel" ]

  initialize() {
    this.close = this.close.bind(this)
    this.inline = matchMedia("(min-width: 60rem)")
  }

  panelTargetConnected() {
    this.inline.addEventListener("change", this.close)
    document.addEventListener("turbo:before-cache", this.close)
  }

  panelTargetDisconnected() {
    document.removeEventListener("turbo:before-cache", this.close)
    this.inline.removeEventListener("change", this.close)
  }

  open() {
    this.panelTarget.showModal()
  }

  close() {
    this.panelTarget.close()
  }

  closeOnBackdrop({ target }) {
    if (target === this.panelTarget) this.close()
  }
}
