import { Controller } from "@hotwired/stimulus"
import { browserFactsOf, sharedJournal } from "#skyblip/journal"

const CONTROLS = "a[href], button, summary, input[type=submit], [role=button], [role=tab]"
const ORIGIN = new RegExp(`${location.origin.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`, "g")
const FRAMES = 5

let listening = false
let opened = false
let visit = "load"
let visitedAt = null
let told = new Set()

export default class extends Controller {
  static values = { chatter: Boolean }

  connect() {
    this.journal = sharedJournal()
    listen(this.journal)
    told = new Set()
    if (this.chatterValue) this.#viewed()
    this.journal.flush()
  }

  clicked({ target }) {
    const control = target.closest?.(CONTROLS)
    if (this.chatterValue && control) this.journal.record("browser.clicked", { control: controlOf(control) })
  }

  #viewed() {
    if (!opened) {
      opened = true
      this.journal.record("browser.opened", {
        bluetooth: "bluetooth" in navigator, standalone: matchMedia("(display-mode: standalone)").matches || navigator.standalone === true,
        online: navigator.onLine, storage: storageWorks(), ...browserFactsOf(navigator),
      })
    }
    const loadMs = Math.round(performance.now() - (visitedAt ?? 0))
    this.journal.record("browser.viewed", { navigation: visit, load_ms: loadMs })
  }
}

function listen(journal) {
  if (listening) return
  listening = true
  document.addEventListener("turbo:visit", ({ detail }) => {
    visit = detail.action === "restore" ? "restore" : "visit"
    visitedAt = performance.now()
  })
  addEventListener("error", event => failed(journal, "error", event.error, event))
  addEventListener("unhandledrejection", event => failed(journal, "rejection", event.reason))
  addEventListener("stimulus:failed", ({ detail }) => failed(journal, "stimulus", detail.error, { controller: detail.identifier }))
  addEventListener("online", () => journal.flush())
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "hidden") journal.flush()
  })
}

// INFO: fc 14nov26 a page that fails on every morph would send a beacon each time, so it tells each failure once per view
function failed(journal, kind, error, { filename, lineno, colno, message, controller } = {}) {
  const facts = {
    kind, error: error?.name ?? "Error", message: error?.message ?? message ?? String(error ?? ""),
    file: filename?.replace(ORIGIN, ""), line: lineno, column: colno, controller, frames: framesOf(error),
  }
  const once = [kind, facts.error, facts.message, facts.file].join("|")
  if (told.has(once)) return
  told.add(once)
  journal.record("browser.failed", facts)
  journal.flush()
}

function framesOf(error) {
  if (typeof error?.stack !== "string") return undefined
  return error.stack.split("\n").map(line => line.trim()).filter(line => /:\d+:\d+\)?$/.test(line)).slice(0, FRAMES).map(line => line.replace(ORIGIN, ""))
}

function controlOf(element) {
  const tag = element.tagName.toLowerCase()
  const name = element.dataset.action?.split(/\s+/)[0] ?? element.id
  if (name) return `${tag} ${name}`.slice(0, 100)
  if (tag === "a") return `a ${new URL(element.href, location.href).pathname}`.slice(0, 100)
  if (element.form?.action) return `${tag} ${new URL(element.form.action, location.href).pathname}`.slice(0, 100)
  return tag
}

function storageWorks() {
  try {
    localStorage.setItem("skyblip.storage.check", "1")
    localStorage.removeItem("skyblip.storage.check")
    return true
  } catch (error) {
    console.warn("This browser keeps no site data:", error.message)
    return false
  }
}
