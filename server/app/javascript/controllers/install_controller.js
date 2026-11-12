import { Controller } from "@hotwired/stimulus"

let offered = null

addEventListener("beforeinstallprompt", event => {
  event.preventDefault()
  offered = event
  document.dispatchEvent(new CustomEvent("briefing:installable"))
})

export default class extends Controller {
  static targets = ["offer"]

  connect() {
    this.shown = () => this.#show()
    document.addEventListener("briefing:installable", this.shown)
    this.#show()
  }

  disconnect() {
    document.removeEventListener("briefing:installable", this.shown)
  }

  async prompt() {
    const event = offered
    offered = null
    this.#show()
    await event?.prompt()
  }

  #show() {
    this.offerTarget.hidden = !offered
  }
}
