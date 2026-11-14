import { Controller } from "@hotwired/stimulus"
import { sharedJournal } from "#skyblip/journal"

export default class extends Controller {
  connect() {
    this.element.textContent = sharedJournal().supportCode
  }
}
