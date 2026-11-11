import { Controller } from "@hotwired/stimulus"

export default class extends Controller {
  static targets = ["device"]
  static values = { src: String }

  async connect() {
    const { LINKED_PHASES, joinSession } = await import(this.srcValue)
    if (!this.element.isConnected) return
    this.linked = LINKED_PHASES
    this.session = joinSession(state => this.#render(state))
    this.#render(this.session.updater.state)
  }

  disconnect() {
    this.session?.leave()
  }

  unlink() {
    this.session.updater.disconnect()
  }

  #render({ phase, device }) {
    this.element.hidden = !this.linked.has(phase)
    this.deviceTarget.textContent = device ?? ""
  }
}
