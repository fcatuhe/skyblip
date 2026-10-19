import { Controller } from "@hotwired/stimulus"

const RECHECK_MS = 5000
const KILOBYTE = 1000
const MS_PER_MINUTE = 60_000
const ESTIMATE_AFTER_SHARE = 0.05

const ASKED_STEPS = new Set(["dfu", "install"])
const PHASE_STEP = { uploading: "upload", installing: "install", rebooting: "install" }
const PHASE_WORD = { connecting: "connecting", recovering: "recovering" }
const LINKED = new Set(["ready", "asking", "confirming", "uploading", "installing"])
const IMAGE_NOTES = { probation: "probation", reverted: "reverted", refused: "image_refused" }
const NUMBERS = new Set(["aircraft_type", "units", "alarm_volume"])
const FILE_ENDING = { slim: ".signed.bin", full: ".full.signed.bin" }

export default class extends Controller {
  static targets = ["unsupported", "connect", "disconnect", "hint", "file", "shelf", "release", "install", "recover",
                    "device", "firmware", "track", "battery", "flight", "advice", "chosen", "notes", "step",
                    "progress", "progressText", "message", "settings", "fields", "default", "save", "reset"]
  static values = { src: String, remaining: String, charging: String, full: String, slim: String, labels: Object }

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
    this.selection?.abort()
    this.updater?.disconnect()
  }

  shelfTargetConnected() {
    if (this.updater) this.#render(this.updater.state)
  }

  link() {
    this.updater.connect()
  }

  unlink() {
    this.updater.disconnect()
  }

  choose() {
    if (this.selecting) return
    const [file] = this.fileTarget.files
    if (!file) return
    this.#shelve(null)
    return this.#selectImage(async () => new Uint8Array(await file.arrayBuffer()), file.name)
  }

  take() {
    if (this.selecting) return
    const option = this.shelfTarget.selectedOptions[0]
    if (!option?.value) {
      this.#shelve(null)
      if (!this.fileTarget.value) this.updater.clearImage()
      return
    }
    this.fileTarget.value = ""
    this.#shelve(option)
    return this.#selectImage(async signal => {
      const response = await fetch(option.value, { signal })
      if (!response.ok) throw new Error(`HTTP ${response.status}`)
      return new Uint8Array(await response.arrayBuffer())
    }, option.value.split("/").pop())
  }

  install() {
    if (!this.selecting) {
      this.estimate = null
      this.updater.install()
    }
  }

  recover() {
    this.updater.recover()
  }

  edit() {
    this.#settings(this.updater.state)
  }

  save(event) {
    event.preventDefault()
    this.updater.saveSettings(this.#edited())
  }

  reset() {
    this.updater.resetSettings()
  }

  async #selectImage(read, name) {
    // INFO: fc 17oct26 selections are serialized and installation stays disabled until the new image is fully read and checked.
    this.selecting = true
    this.selection = new AbortController()
    this.updater.clearImage()
    let failed = false
    try {
      const bytes = await read(this.selection.signal)
      if (!this.selection.signal.aborted) await this.updater.choose(bytes, name)
    } catch (error) {
      failed = error.name !== "AbortError"
    } finally {
      this.selecting = false
      if (this.element.isConnected) {
        this.#render(this.updater.state)
        if (failed) {
          this.#shelve(null)
          this.messageTarget.textContent = this.#word("shelf_failed")
        }
      }
    }
  }

  #render(state) {
    const linked = LINKED.has(state.phase)
    const idle = state.phase === "ready" && this.updater.onGround
    this.connectTarget.hidden = linked
    this.connectTarget.disabled = !this.bluetooth || state.phase === "connecting"
    this.disconnectTarget.hidden = !linked
    this.hintTarget.hidden = linked
    this.installTarget.disabled = this.selecting || !idle || !state.file
    this.fileTarget.disabled = this.selecting || !["offline", "ready"].includes(state.phase)
    this.#offer(state, idle)
    this.recoverTarget.disabled = !idle
    this.#facts(state)
    this.#notes(state)
    this.#settings(state)
    this.#steps(state)
    this.#progress(state)
    this.messageTarget.textContent = this.#message(state)
    this.messageTarget.classList.toggle("visually-hidden", !state.notice && Boolean(this.#step(state)))
    this.#recheck(state)
  }

  #offer({ image }, idle) {
    if (!this.hasShelfTarget) return
    const options = [...this.shelfTarget.options].filter(option => option.value)
    const channel = image?.downgrade ? "development" : "release"
    // INFO: fc 17oct26 release channels filter choices. The updater verifies the downloaded image's signing key before installation.
    for (const option of options) option.hidden = !image?.key || option.dataset.channel !== channel
    if (this.shelfTarget.selectedOptions[0]?.hidden) {
      this.shelfTarget.value = ""
      this.#shelve(null)
    }
    this.shelfTarget.disabled = this.selecting || !idle || options.every(option => option.hidden)
  }

  #shelve(option) {
    if (!this.hasShelfTarget) return
    if (!option) this.shelfTarget.value = ""
    this.releaseTarget.hidden = !option
    if (option) this.releaseTarget.href = option.dataset.release
  }

  #facts({ device, running, image, status, advice, file }) {
    this.deviceTarget.textContent = device || "-"
    this.firmwareTarget.textContent = running || "-"
    this.trackTarget.textContent = image?.key ? this.labelsValue.track[image.downgrade ? "development" : "release"] : "-"
    this.adviceTarget.textContent = advice ? `${this.#kind(advice === "full")}, ${FILE_ENDING[advice]}` : "-"
    this.chosenTarget.textContent = file ? this.#chosen(file) : "-"
    const percent = status?.batteryPercent
    const charging = status?.charging ? ` ${this.chargingValue}` : ""
    this.batteryTarget.textContent = percent === null || percent === undefined ? "-" : `${percent} %${charging}`
    this.flightTarget.textContent = status?.flight ? this.labelsValue.flight[status.flight] ?? status.flight : "-"
  }

  #chosen({ version, full }) {
    if (full === null) return version
    return `${version}, ${this.#kind(full)}`
  }

  #kind(full) {
    return full ? this.fullValue : this.slimValue
  }

  #notes({ image, status, settings, address, activates }) {
    const notes = []
    if (settings && activates && address === null) notes.push(["not_activated"])
    if (status && status.flight !== "ground") notes.push(["in_flight"])
    if (IMAGE_NOTES[image?.state]) notes.push([IMAGE_NOTES[image.state], image.to])
    if (image?.imu === "writing") notes.push(["imu_writing"])
    if (image && !image.key && this.hasShelfTarget) notes.push(["no_key"])
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

  #settings({ phase, settings, defaults }) {
    this.settingsTarget.hidden = !settings
    if (!settings) return
    if (settings !== this.shown) this.#fill(settings)
    const edited = this.#edited()
    const changed = Object.keys(edited).some(key => edited[key] !== settings[key])
    const idle = phase === "ready" && this.updater.onGround
    this.fieldsTarget.disabled = !idle
    this.saveTarget.disabled = !idle || !changed
    this.resetTarget.hidden = !defaults
    this.resetTarget.disabled = !idle || !defaults || Object.keys(defaults).every(key => defaults[key] === settings[key])
    for (const note of this.defaultTargets) this.#default(note, settings, defaults)
  }

  #fill(settings) {
    this.shown = settings
    const { elements } = this.settingsTarget
    this.#choose(elements.aircraft_type, settings.aircraft_type)
    elements.callsign.value = settings.callsign
    elements.units.value = settings.units
    elements.alarm.checked = settings.alarm
    this.#choose(elements.alarm_volume, settings.alarm_volume)
  }

  #choose(select, value) {
    if (![...select.options].some(option => Number(option.value) === value)) {
      select.add(new Option(String(value), value))
    }
    select.value = value
  }

  #edited() {
    const { elements } = this.settingsTarget
    const edited = {}
    for (const key of Object.keys(this.shown ?? {})) {
      const field = elements[key]
      if (field.type === "checkbox") edited[key] = field.checked
      else if (NUMBERS.has(key)) edited[key] = Number(field.value)
      else edited[key] = field.value.toUpperCase().trimEnd()
    }
    return edited
  }

  #default(note, settings, defaults) {
    const key = note.dataset.setting
    const differs = Boolean(defaults) && key in defaults && defaults[key] !== settings[key]
    note.hidden = !differs
    note.textContent = differs ? this.labelsValue.default.replace("%{value}", this.#shown(key, defaults[key])) : ""
  }

  #shown(key, value) {
    const field = this.settingsTarget.elements[key]
    if (field.type === "checkbox") return value ? this.labelsValue.on : this.labelsValue.off
    if (field.tagName === "SELECT") return [...field.options].find(option => Number(option.value) === value)?.text ?? String(value)
    if (value === "") return this.labelsValue.none
    return String(value)
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
    if (state.task === "set") return this.#word("confirm_set")
    const step = this.#step(state)
    if (step) return this.#word(`step_${step}`)
    return PHASE_WORD[state.phase] ? this.#word(PHASE_WORD[state.phase]) : ""
  }

  #word(key) {
    const word = document.querySelector(`[data-manage-word="${CSS.escape(key)}"]`)
    if (word) return word.textContent.replace(/[ \t\r\n]+/g, " ").trim()
    return key === "refused" ? key : `${this.#word("refused")} ${key}`
  }

  #recheck({ settling }) {
    clearTimeout(this.recheck)
    if (settling) this.recheck = setTimeout(() => this.updater.refresh(), RECHECK_MS)
  }
}
