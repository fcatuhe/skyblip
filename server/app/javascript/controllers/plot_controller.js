import { Controller } from "@hotwired/stimulus"

export default class extends Controller {
  static targets = [ "choices", "choice" ]
  static values = { view: String }

  connect() {
    this.choicesTarget.hidden = false
    this.viewValue = "all"
  }

  show({ params: { view } }) {
    this.viewValue = view
  }

  viewValueChanged(view) {
    for (const choice of this.choiceTargets)
      choice.setAttribute("aria-pressed", choice.dataset.plotViewParam === view)
  }
}
