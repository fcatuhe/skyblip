import { compareVersions, parseVersion } from "#skyblip/image"
import { productOf, trustedKeys } from "#skyblip/update"

const offered = new WeakMap()
const taking = new WeakMap()
const picks = new WeakMap()

export function shelfIn(element) {
  return element.querySelector("select[data-shelf]")
}

export function channelOf(unit) {
  return unit.state?.image?.downgrade ? "development" : "release"
}

export function channelOfImage(file, unit) {
  return file?.channel ?? channelOf(unit)
}

export function fitting(shelf, unit) {
  const image = unit.state?.image
  const trusted = trustedKeys(image)
  if (!shelf || !trusted) return []
  return [...shelf.options].filter(option => option.value && trusted.includes(option.dataset.key) && option.dataset.product === productOf(unit.state) && !older(option, unit))
}

// INFO: fc 15nov26 a stable tag has no build number, so it is held to the running version's triple
function older(option, { state }) {
  const offered = parseVersion(option.dataset.version)
  const running = parseVersion(state?.running)
  if (state?.image?.downgrade || !offered || !running) return false
  const floor = /^\d+\.\d+\.\d+[.+]\d+$/.test(option.dataset.version) ? running : { ...running, build: 0 }
  return compareVersions(offered, floor) < 0
}

// INFO: fc 11nov26 once per connection, the newest base image of the unit's track and product is read, so the page knows whether it is behind
export function offerNewest(unit, shelf) {
  const { device, settings, notice, file } = unit.state ?? {}
  if (!device) offered.delete(unit)
  if (!device || !settings || notice || file || offered.get(unit) === device || taking.has(unit)) return null
  const newest = fitting(shelf, unit).find(option => option.dataset.kind === "base" && option.dataset.channel === channelOf(unit))
  if (!newest) return null
  offered.set(unit, device)
  picks.set(unit, false)
  return take(unit, newest)
}

export function taken(unit) {
  return taking.get(unit) ?? null
}

export function pick(unit, option, read) {
  picks.set(unit, true)
  return take(unit, option, read)
}

export function picked(unit) {
  return picks.get(unit) ?? false
}

export async function take(unit, option, read = fetchImage) {
  const selection = new AbortController()
  taking.get(unit)?.controller.abort()
  const work = { controller: selection, option }
  taking.set(unit, work)
  unit.updater.clearImage()
  try {
    const bytes = await read(option.value, selection.signal)
    if (!selection.signal.aborted) await unit.updater.choose(bytes, option.value.split("/").pop(), { channel: option.dataset?.channel ?? null })
    return { ok: true }
  } catch (error) {
    return { ok: error.name === "AbortError" }
  } finally {
    if (taking.get(unit) === work) taking.delete(unit)
  }
}

async function fetchImage(url, signal) {
  const response = await fetch(url, { signal })
  if (!response.ok) throw new Error(`HTTP ${response.status}`)
  return new Uint8Array(await response.arrayBuffer())
}
