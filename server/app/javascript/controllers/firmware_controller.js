import { Controller } from "@hotwired/stimulus"
import { joinFleet } from "#skyblip/fleet"
import { fitting, offerNewest, shelfIn, take, taken } from "controllers/briefing/shelf"
import { productWord } from "controllers/briefing/facts"
import { installCause, reportShown } from "#skyblip/install_report"
import { say, sayNotice } from "controllers/briefing/words"

const RECHECK_MS = 5000
const KILOBYTE = 1000
const MS_PER_MINUTE = 60_000
const ESTIMATE_AFTER_SHARE = 0.05
const IMAGE_NOTES = { probation: "probation", reverted: "reverted", refused: "image_refused" }
const WORKING = new Set(["asking", "confirming", "uploading", "installing", "rebooting", "recovering"])
const REFUSALS = new Set(["cancelled", "refused_on_device"])

export default class extends Controller {
  static targets = ["panel", "status", "progress", "bar", "progressText", "message", "report", "install", "back", "other", "file", "release", "recovery", "recover"]
  static values = { slug: String }
  static outlets = ["glass"]

  connect() {
    this.session = joinFleet(() => this.#render())
    this.fleet = this.session.fleet
    this.framed = () => this.#render()
    this.element.addEventListener("turbo:frame-load", this.framed)
    this.#render()
  }

  disconnect() {
    clearTimeout(this.recheck)
    this.element.removeEventListener("turbo:frame-load", this.framed)
    this.session.leave()
  }

  get unit() {
    return this.fleet.unit(this.slugValue)
  }

  get shelf() {
    return shelfIn(this.element)
  }

  async take() {
    this.picked = true
    const option = this.shelf.selectedOptions[0]
    this.fileTarget.value = ""
    if (!option?.value) return this.unit.updater.clearImage()
    await this.#taking(take(this.unit, option))
  }

  async choose() {
    const [file] = this.fileTarget.files
    if (!file) return
    this.picked = true
    if (this.shelf) this.shelf.value = ""
    await this.#taking(take(this.unit, { value: file.name }, async () => new Uint8Array(await file.arrayBuffer())))
  }

  install() {
    if (taken(this.unit)) return
    this.estimate = null
    this.unit.attempted = this.unit.state.file?.version
    this.unit.restarted = false
    this.unit.updater.install()
  }

  recover() {
    this.unit.updater.recover()
  }

  async #taking(work) {
    this.#render()
    const { ok } = await work
    this.shelfFailed = !ok
    this.#render()
  }

  #render() {
    const unit = this.unit
    const state = unit?.state
    const shown = Boolean(state) && (unit.linked || WORKING.has(state.phase) || Boolean(unit.attempted && state.phase !== "offline"))
    this.panelTarget.hidden = !shown
    clearTimeout(this.recheck)
    if (!shown) return this.glassOutlet?.hide()
    if (state.phase === "rebooting") unit.restarted = true
    const idle = state.phase === "ready" && state.status?.flight !== "airborne"
    const asks = idle && !state.refusedOnDevice
    if (unit.linked) offerNewest(unit, this.shelf)?.then(() => this.#render())
    this.#upgradeToFull(unit, state)
    this.#offer(unit, state, idle)
    this.#install(unit, state, asks)
    this.recoverTarget.disabled = !asks
    const busy = WORKING.has(state.phase) || state.image?.state === "probation" || state.image?.imu === "writing"
    this.otherTarget.hidden = busy
    this.recoveryTarget.hidden = busy
    const done = this.#done(state)
    this.statusTarget.textContent = this.#status(state, unit)
    this.statusTarget.classList.toggle("is-good", done)
    this.backTarget.hidden = !done
    this.fileTarget.disabled = !idle || Boolean(taken(unit))
    this.#progress(unit, state)
    this.#message(state)
    this.#glass(unit, state)
    if (state.settling) this.recheck = setTimeout(() => unit.updater.refresh(), RECHECK_MS)
  }

  #offer(unit, state, idle) {
    const shelf = this.shelf
    if (!shelf) return
    const fits = new Set(fitting(shelf, unit))
    for (const option of shelf.options) if (option.value) option.hidden = !fits.has(option)
    shelf.disabled = !idle || fits.size === 0 || Boolean(taken(unit))
    const chosen = [...fits].find(option => option.value.split("/").pop() === state.file?.name)
    if (chosen) shelf.value = chosen.value
    this.releaseTarget.hidden = !chosen
    if (chosen) this.releaseTarget.href = chosen.dataset.release
  }

  // INFO: fc 11nov26 a slim image for a hub that lacks its blob would install, then revert, so the page swaps in the full one of that release
  #upgradeToFull(unit, state) {
    if (state.notice?.key !== "needs_full" || taken(unit)) return
    const slim = [...(this.shelf?.options ?? [])].find(option => option.value.split("/").pop() === state.file?.name)
    const full = slim && [...this.shelf.options].find(option => option.dataset.release === slim.dataset.release && option.dataset.product === slim.dataset.product && option.dataset.kind === "full")
    if (full) this.#taking(take(unit, full))
  }

  #install(unit, state, asks) {
    const { file, advice } = state
    const airborne = state.status?.flight === "airborne"
    this.installTarget.disabled = !asks || !file || Boolean(taken(unit))
    const settling = state.image?.state === "probation" || state.image?.imu === "writing"
    this.installTarget.hidden = state.refusedOnDevice || (WORKING.has(state.phase) && state.phase !== "asking") || settling || this.#done(state) || (state.upgrade === false && !this.picked)
    if (airborne) this.installTarget.textContent = say("words.locked")
    else if (file) this.installTarget.textContent = say(file.full && advice === "full" ? "words.install_full" : "words.install_version", { version: file.version })
    else this.installTarget.textContent = this.installTarget.dataset.label
  }

  #done({ running, image }) {
    return Boolean(this.unit.attempted) && running === this.unit.attempted && image?.state === "confirmed"
  }

  #status(state, unit) {
    const { phase, file, upgrade, image, running, progress } = state
    if (phase === "uploading") return say("words.sending", { version: file?.version ?? "" })
    if (phase === "installing") return say("words.installing")
    if (phase === "rebooting") return `${say("words.installing")} ${say("words.reconnecting")}`
    if (phase === "recovering") return say("notices.recovering")
    if (image?.state === "probation") return say("words.probation", { version: running ?? image.to ?? "" })
    if (this.#done(state)) return say("words.done", { version: running })
    if (taken(unit)) return ""
    if (upgrade === false) return say("words.newest", { version: running })
    if (!file && this.shelf && fitting(this.shelf, unit).length === 0) return say("words.no_image")
    return ""
  }

  #progress(unit, { phase, progress }) {
    const shown = Boolean(progress) && phase === "uploading"
    this.progressTarget.hidden = !shown
    if (!shown) return
    this.barTarget.max = progress.total
    this.barTarget.value = progress.sent
    const percent = Math.floor(100 * progress.sent / progress.total)
    const sent = say("words.sent", { percent, sent: Math.floor(progress.sent / KILOBYTE), total: Math.ceil(progress.total / KILOBYTE) })
    const minutes = this.#minutesLeft(progress)
    const left = minutes ? say("words.left", { minutes }) : say("words.estimating")
    const product = productWord(unit)
    this.progressTextTarget.replaceChildren(...[`${sent} · ${left}`, say("words.keep_close", { product }), say("words.stops", { product })]
      .map(line => Object.assign(document.createElement("span"), { textContent: line })))
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
    const lines = []
    const { notice, image, status } = state
    const refusedHere = state.refusedOnDevice && REFUSALS.has(notice?.key)
    if (notice && !notice.during && notice.key !== "needs_full" && !refusedHere) lines.push(sayNotice(notice, "notices"))
    if (this.shelfFailed) lines.push(say("notices.shelf_failed"))
    const attempted = this.unit.restarted ? this.unit.attempted : null
    const cause = installCause({ image, report: state.installReport, running: state.running, attempted })
    if (cause) lines.push(say(`notices.install_report.${cause}`, { running: state.running, attempted }))
    else if (IMAGE_NOTES[image?.state] && image.state !== "probation") lines.push(`${say(`notices.${IMAGE_NOTES[image.state]}`)} ${image.to ?? ""}`.trim())
    if (image?.imu === "writing") lines.push(say("notices.imu_writing"))
    if (image && !image.key) lines.push(say("notices.no_key"))
    if (image && !image.swapPowered) lines.push(say("notices.swap_unpowered"))
    if (status?.wentDarkFlat) lines.push(say("notices.went_dark_flat"))
    this.#report(state, cause)
    this.messageTarget.hidden = lines.length === 0
    const drawn = lines.join("\n")
    if (drawn === this.drawnMessage) return
    this.drawnMessage = drawn
    this.messageTarget.replaceChildren(...lines.map(line => Object.assign(document.createElement("span"), { textContent: line })))
  }

  // INFO: fc 14nov26 the journal sends the install report as device.install_reported, so the page only says it was received
  #report(state, cause) {
    this.reportTarget.hidden = !reportShown({ image: state.image, cause })
  }

  #glass(unit, state) {
    if (!this.hasGlassOutlet) return
    const { phase, task, file } = state
    const lamp = unit.product === "skyblip_bravo"
    const product = productWord(unit)
    if (phase === "confirming" && task === "dfu") this.glassOutlet.show({ title: "FIRMWARE", prompt: "dfu", argument: file?.version ?? "", product, lamp, remembers: state.trust === "proven" })
    else if (phase === "confirming" && task === "recovery") this.glassOutlet.show({ title: "RECOVERY", prompt: "recovery", product, lamp })
    else if (task !== "set") this.glassOutlet.hide()
  }

}
