import { Controller } from "@hotwired/stimulus"
import { mountSimulator } from "#skyblip/dev_simulator"

export default class extends Controller {
  static targets = ["error"]
  static values = { src: String, manifest: String }

  async connect() {
    const session = this.session = new AbortController()
    try {
      const controls = await mountSimulator(this.element, this.srcValue, this.manifestValue, session.signal)
      if (!session.signal.aborted) this.controls = controls
    } catch (error) {
      if (!session.signal.aborted) this.errorTarget.textContent = error.message
    }
  }

  disconnect() {
    this.session.abort()
    this.controls = null
  }

  async control(event) {
    if (this.controls) {
      try {
        await this.controls[event.params.command](event.params.value ?? event.target.value)
      } catch (error) {
        this.errorTarget.textContent = error.message
      }
    }
  }

  press(event) {
    if (this.controls && this.accepts(event)) {
      event.preventDefault()
      this.controls[event.params.command](1)
    }
  }

  release(event) {
    if (this.controls && this.accepts(event)) {
      event.preventDefault()
      this.controls[event.params.command](0)
    }
  }

  accepts(event) {
    return !event.type.startsWith("key") || (!event.repeat && [" ", "Enter"].includes(event.key))
  }
}
