import { Controller } from "@hotwired/stimulus"

const PROBATION_RECHECK_MS = 5000
const KILOBYTE = 1000
const MS_PER_MINUTE = 60_000
const ESTIMATE_AFTER_SHARE = 0.05

const ASKED_STEPS = new Set(["dfu", "apply"])
const PHASE_STEP = { uploading: "upload", installing: "install", rebooting: "install" }
const PHASE_WORD = { connecting: "connecting", recovering: "recovering" }
const LINKED = new Set(["ready", "asking", "confirming", "uploading", "installing"])

export default class extends Controller {
  static targets = ["unsupported", "connect", "disconnect", "hint", "file", "install", "recover",
                    "device", "firmware", "battery", "chosen", "notes", "step",
                    "progress", "progressText", "message"]
  static values = { src: String, remaining: String, charging: String }

  async connect() {
    const { Updater, hasWebBluetooth } = await import(this.srcValue)
    if (!this.element.isConnected) return
    this.bluetooth = hasWebBluetooth()
    this.unsupportedTarget.hidden = this.bluetooth
    this.updater = new Updater({ onChange: state => this.#render(state) })
    this.#render(this.updater.state)
  }

  disconnect() {
    clearTimeout(this.recheck)
    this.updater?.disconnect()
  }

  link() {
    this.updater.connect()
  }

  unlink() {
    this.updater.disconnect()
  }

  async choose() {
    const [file] = this.fileTarget.files
    if (file) await this.updater.choose(new Uint8Array(await file.arrayBuffer()), file.name)
  }

  install() {
    this.estimate = null
    this.updater.install()
  }

  recover() {
    this.updater.recover()
  }

  #render(state) {
    const linked = LINKED.has(state.phase)
    const idle = state.phase === "ready"
    this.connectTarget.hidden = linked
    this.connectTarget.disabled = !this.bluetooth || state.phase === "connecting"
    this.disconnectTarget.hidden = !linked
    this.hintTarget.hidden = linked
    this.installTarget.disabled = !idle || !state.file
    this.recoverTarget.disabled = !idle
    this.#facts(state)
    this.#notes(state)
    this.#steps(state)
    this.#progress(state)
    this.messageTarget.textContent = this.#message(state)
    this.messageTarget.classList.toggle("visually-hidden", !state.notice && Boolean(this.#step(state)))
    this.#recheckProbation(state)
  }

  #facts({ device, running, status, file }) {
    this.deviceTarget.textContent = device || "-"
    this.firmwareTarget.textContent = running || "-"
    this.chosenTarget.textContent = file ? file.version : "-"
    const percent = status?.batteryPercent
    const charging = status?.charging ? ` ${this.chargingValue}` : ""
    this.batteryTarget.textContent = percent === null || percent === undefined ? "-" : `${percent} %${charging}`
  }

  #notes({ image, status }) {
    const notes = []
    if (image?.state === "probation" || image?.state === "reverted") notes.push([image.state, image.to])
    if (image?.settings) notes.push([`settings_${image.settings}`])
    if (image && !image.swapPowered) notes.push(["swap_unpowered"])
    if (status?.wentDarkFlat) notes.push(["went_dark_flat"])
    this.notesTarget.replaceChildren(...notes.map(([key, version]) => this.#note(key, version)))
  }

  #note(key, version) {
    const note = document.createElement("p")
    note.textContent = this.#word(key)
    if (version) {
      const tried = document.createElement("span")
      tried.className = "mono"
      tried.textContent = version
      note.append(" ", tried)
    }
    return note
  }

  #steps(state) {
    const current = this.#step(state)
    for (const step of this.stepTargets) {
      if (step.dataset.step === current) step.setAttribute("aria-current", "step")
      else step.removeAttribute("aria-current")
    }
  }

  #step({ phase, task }) {
    if (phase === "asking" || phase === "confirming") return ASKED_STEPS.has(task) ? task : null
    return PHASE_STEP[phase] || null
  }

  #progress({ phase, progress }) {
    const shown = Boolean(progress) && phase === "uploading"
    this.progressTarget.hidden = !shown
    this.progressTextTarget.hidden = !shown
    if (!shown) return
    this.progressTarget.max = progress.total
    this.progressTarget.value = progress.sent
    const kilobytes = `${Math.floor(progress.sent / KILOBYTE)} / ${Math.ceil(progress.total / KILOBYTE)} kB`
    const minutes = this.#minutesLeft(progress)
    this.progressTextTarget.textContent = minutes ? `${kilobytes}, ${this.remainingValue.replace("%{minutes}", minutes)}` : kilobytes
  }

  #minutesLeft({ sent, total }) {
    const now = performance.now()
    if (!this.estimate || sent < this.estimate.sent) this.estimate = { sent, at: now }
    const moved = sent - this.estimate.sent
    if (moved < total * ESTIMATE_AFTER_SHARE) return null
    const perMs = moved / (now - this.estimate.at)
    return Math.ceil((total - sent) / perMs / MS_PER_MINUTE)
  }

  #message(state) {
    if (state.notice) return [this.#word(state.notice.key), state.notice.detail].filter(Boolean).join(" ")
    if (state.task === "recovery") return this.#word("confirm_recovery")
    const step = this.#step(state)
    if (step) return this.#word(`step_${step}`)
    return PHASE_WORD[state.phase] ? this.#word(PHASE_WORD[state.phase]) : ""
  }

  #word(key) {
    const word = document.querySelector(`[data-manage-word="${CSS.escape(key)}"]`)
    if (word) return word.textContent.replace(/[ \t\r\n]+/g, " ").trim()
    return key === "refused" ? key : `${this.#word("refused")} ${key}`
  }

  #recheckProbation({ phase, image }) {
    clearTimeout(this.recheck)
    if (phase === "ready" && image?.state === "probation") {
      this.recheck = setTimeout(() => this.updater.refresh(), PROBATION_RECHECK_MS)
    }
  }
}
