import { Controller } from "@hotwired/stimulus"

export default class extends Controller {
  connect() {
    this.open()
  }

  open() {
    const target = document.getElementById(decodeURIComponent(location.hash.slice(1)))
    if (target instanceof HTMLDetailsElement && this.element.contains(target)) target.open = true
  }
}
