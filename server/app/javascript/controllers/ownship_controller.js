import { Controller } from "@hotwired/stimulus"
import * as ownship from "#skyblip/ownship"

const IMAGE_KEY = "skyblip.ownship.image"
const DRAFT_KEY = "skyblip.ownship.draft"
const IMAGE_SIDE = 640
const OWN = "own"

export default class extends Controller {
  static targets = [
    "grid", "ghost", "ghostOutline", "image", "mirror", "outline", "hotSpot", "trace", "draft",
    "sprite", "size", "ink", "nose", "tail", "checks", "diff", "both", "extra", "missing", "fidelity", "ownHint",
    "text", "cpp", "command", "bytes", "reference", "copied", "unreadable", "imageError"
  ]
  static values = { type: Object, shipped: Object, linked: Object, planforms: Array, outline: String, limits: Object, words: Object }

  connect() {
    this.planforms = this.planformsValue.map(ownship.planform)
    if (!this.planforms.some(p => p.key === this.outlineValue) && this.outlineValue !== OWN) this.outlineValue = this.planforms[0].key
    this.outlineTarget.value = this.outlineValue

    const draft = localStorage.getItem(this.#draftKey)
    if (this.linkedValue.words) {
      this.drawing = this.#fromValue(this.linkedValue)
    } else if (draft) {
      this.drawing = ownship.parse(draft)
    } else {
      this.drawing = this.#fromValue(this.shippedValue)
    }

    const image = localStorage.getItem(this.#imageKey)
    if (image) this.imageTarget.src = image
    this.#build({ edited: false })
  }

  chooseType({ target }) {
    target.form.requestSubmit()
  }

  press(event) {
    const cell = event.target.closest(".ownship-cell")
    if (!cell || event.button !== 0) return

    event.preventDefault()
    this.painting = !cell.checked
    this.#paint(cell)
    event.currentTarget.setPointerCapture(event.pointerId)
  }

  drag({ clientX, clientY }) {
    if (this.painting === undefined) return

    const cell = document.elementFromPoint(clientX, clientY)?.closest(".ownship-cell")
    if (cell) this.#paint(cell)
  }

  release() {
    if (this.painting === undefined) return

    this.painting = undefined
    this.#remember()
  }

  click(event) {
    if (event.target.matches(".ownship-cell") && event.detail > 0) event.preventDefault()
  }

  toggle({ target }) {
    if (!target.matches(".ownship-cell")) return

    this.painting = target.checked
    this.#paint(target)
    this.painting = undefined
    this.#remember()
  }

  reshape({ params: { operation } }) {
    this.#replace(ownship.reshape(this.drawing, operation))
  }

  trace() {
    this.#replace(ownship.trace(this.#planform, ownship.width(this.drawing), ownship.height(this.drawing)))
  }

  reset() {
    this.#replace(this.#fromValue(this.shippedValue))
  }

  moveHotSpot({ target }) {
    const row = target.value === "" ? null : Number(target.value)
    this.#replace(ownship.glyph(this.drawing.pixels, row))
  }

  chooseOutline({ target }) {
    this.outlineValue = target.value
    this.#render()
    this.#remember()
  }

  async importImage({ target }) {
    const [file] = target.files
    target.value = ""
    if (!file) return

    let url
    try {
      url = await this.#shrunk(file)
    } catch (error) {
      if (!(error instanceof DOMException)) throw error
      this.imageErrorTarget.textContent = this.wordsValue.image_unreadable
      return
    }
    this.imageErrorTarget.textContent = ""
    this.imageTarget.src = url
    this.outlineValue = OWN
    this.outlineTarget.value = OWN
    this.#render()
    this.#remember()
    try {
      localStorage.setItem(this.#imageKey, url)
    } catch (error) {
      if (error.name !== "QuotaExceededError") throw error
      this.imageErrorTarget.textContent = this.wordsValue.image_unkept
    }
  }

  importText() {
    let drawing
    try {
      drawing = ownship.parse(this.textTarget.value)
    } catch (error) {
      if (!(error instanceof RangeError)) throw error
      this.unreadableTarget.hidden = false
      return
    }
    this.#replace(drawing)
  }

  async paste() {
    this.textTarget.value = await navigator.clipboard.readText()
    this.importText()
  }

  async copy({ params: { source } }) {
    await navigator.clipboard.writeText(this[`${source}Target`].value)
    this.copiedTarget.textContent = this.wordsValue.copied
  }

  get #planform() {
    return this.planforms.find(p => p.key === this.outlineValue) ?? this.planforms[0]
  }

  get #own() {
    return this.outlineValue === OWN
  }

  get #draftKey() {
    return `${DRAFT_KEY}.${this.typeValue.key}`
  }

  get #imageKey() {
    return `${IMAGE_KEY}.${this.typeValue.key}`
  }

  #fromValue({ words, width, hotSpot }) {
    return ownship.fromWords(words, width, hotSpot)
  }

  #paint(cell) {
    const row = Number(cell.dataset.row), column = Number(cell.dataset.column)
    const pixels = this.drawing.pixels.map(r => [...r])
    const columns = this.#mirrored(column)
    const bit = this.painting ? 1 : 0
    if (columns.every(c => this.drawing.pixels[row][c] === bit) && columns.every(c => this.#cell(row, c).checked === this.painting)) return
    columns.forEach(c => { pixels[row][c] = bit })
    this.drawing = ownship.glyph(pixels, this.drawing.given)
    columns.forEach(c => { this.#cell(row, c).checked = this.painting })
    this.#render()
  }

  #mirrored(column) {
    const twin = ownship.width(this.drawing) - 1 - column
    return this.mirrorTarget.checked && twin !== column ? [column, twin] : [column]
  }

  #replace(drawing) {
    this.drawing = drawing
    this.#build()
  }

  #build({ edited = true } = {}) {
    const w = ownship.width(this.drawing)
    this.gridTarget.querySelectorAll(".ownship-row").forEach(row => row.remove())
    this.drawing.pixels.forEach((bits, r) => {
      const row = document.createElement("div")
      row.className = "ownship-row"
      bits.forEach((bit, c) => {
        const cell = document.createElement("input")
        cell.type = "checkbox"
        cell.className = c === w >> 1 ? "ownship-cell ownship-cell--axis" : "ownship-cell"
        cell.checked = bit === 1
        cell.dataset.row = r
        cell.dataset.column = c
        cell.setAttribute("aria-label", this.#say(this.wordsValue.cell, { row: r + 1, column: c + 1 }))
        row.append(cell)
      })
      this.gridTarget.append(row)
    })
    this.#render()
    this.#remember({ store: edited })
  }

  #render() {
    const g = this.drawing, w = ownship.width(g), h = ownship.height(g), spot = ownship.hotSpot(g)
    const words = this.wordsValue

    this.gridTarget.querySelectorAll(".ownship-row").forEach((row, r) => row.classList.toggle("ownship-row--hot-spot", r === spot))
    this.gridTarget.dataset.outline = this.outlineValue
    this.ghostTarget.setAttribute("viewBox", `0 0 ${w} ${h}`)
    this.ghostOutlineTarget.setAttribute("points", this.#own ? "" : ownship.ghostPoints(this.#planform, w, h))
    this.hotSpotTarget.max = h - 1
    this.hotSpotTarget.placeholder = spot
    this.hotSpotTarget.value = g.given ?? ""
    this.traceTarget.hidden = this.#own

    this.spriteTarget.setAttribute("d", ownship.runsPath(g.pixels, 100 - (w >> 1), 99 - spot))
    this.sizeTarget.textContent = this.#say(words.size, { width: w, height: h })
    this.inkTarget.textContent = this.#say(words.ink, { count: ownship.ink(g) })
    this.noseTarget.textContent = this.#rows(spot)
    this.tailTarget.textContent = this.#rows(ownship.rowsAft(g))
    this.#renderChecks()
    this.#renderFidelity()

    this.textTarget.value = ownship.toText(g)
    this.cppTarget.value = ownship.toCpp(g)
    this.commandTarget.value = ownship.toCommand(g, this.typeValue.code)
    this.bytesTarget.textContent = this.#say(words.command_hint, { bytes: this.commandTarget.value.length })
    this.copiedTarget.textContent = ""
    this.unreadableTarget.hidden = true
    this.referenceTargets.forEach(section => { section.hidden = section.dataset.outline !== this.outlineValue })
  }

  #renderChecks() {
    const found = ownship.problems(this.drawing, this.limitsValue)
    const items = found.length
      ? found.map(({ key, details }) => this.#item(this.#say(this.wordsValue.problems[key], details)))
      : [this.#item(this.wordsValue.fits, "ownship-check--ok")]
    this.checksTarget.replaceChildren(...items)
  }

  #renderFidelity() {
    this.diffTarget.hidden = this.#own
    this.ownHintTarget.hidden = !this.#own
    if (this.#own) return

    const paths = ownship.diffPaths(this.#planform, this.drawing)
    const score = ownship.fidelity(this.#planform, this.drawing)
    const percent = share => `${Math.round(share * 100)}%`
    this.diffTarget.querySelector("svg").setAttribute("viewBox", `0 0 ${ownship.width(this.drawing)} ${ownship.height(this.drawing)}`)
    this.bothTarget.setAttribute("d", paths.both)
    this.extraTarget.setAttribute("d", paths.extra)
    this.missingTarget.setAttribute("d", paths.missing)
    this.fidelityTarget.textContent = this.#say(this.wordsValue.fidelity, {
      iou: score.iou.toFixed(2), missing: percent(score.missing), extra: percent(score.extra)
    })
  }

  #remember({ store = true } = {}) {
    if (store) {
      const text = ownship.toText(this.drawing)
      if (text === ownship.toText(this.#fromValue(this.shippedValue))) {
        localStorage.removeItem(this.#draftKey)
      } else {
        localStorage.setItem(this.#draftKey, text)
      }
    }
    this.draftTarget.hidden = localStorage.getItem(this.#draftKey) !== ownship.toText(this.drawing)

    const url = new URL(location.href)
    url.search = new URLSearchParams({ type: this.typeValue.key, outline: this.outlineValue, ...ownship.toParams(this.drawing) })
    history.replaceState(history.state, "", url)
  }

  #cell(row, column) {
    return this.gridTarget.querySelectorAll(".ownship-row")[row].children[column]
  }

  #item(text, modifier) {
    const item = document.createElement("li")
    item.className = modifier ? `ownship-check ${modifier}` : "ownship-check"
    item.textContent = text
    return item
  }

  #rows(count) {
    return this.#say(count === 1 ? this.wordsValue.rows_count.one : this.wordsValue.rows_count.other, { count })
  }

  #say(template, values) {
    return template.replace(/%\{(\w+)\}/g, (_, key) => values[key])
  }

  async #shrunk(file) {
    const bitmap = await createImageBitmap(file)
    const scale = Math.min(1, IMAGE_SIDE / Math.max(bitmap.width, bitmap.height))
    const canvas = document.createElement("canvas")
    canvas.width = Math.round(bitmap.width * scale)
    canvas.height = Math.round(bitmap.height * scale)
    canvas.getContext("2d").drawImage(bitmap, 0, 0, canvas.width, canvas.height)
    return canvas.toDataURL("image/png")
  }
}
