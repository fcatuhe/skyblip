import { productOf } from "#skyblip/update"

const offered = new WeakMap()
const taking = new WeakMap()

export function shelfIn(element) {
  return element.querySelector("select[data-shelf]")
}

export function channelOf(unit) {
  return unit.state?.image?.downgrade ? "development" : "release"
}

export function fitting(shelf, unit) {
  const image = unit.state?.image
  if (!shelf || !image?.key) return []
  return [...shelf.options].filter(option => option.value && option.dataset.channel === channelOf(unit) && option.dataset.product === productOf(image))
}

// INFO: fc 11nov26 once per connection, the newest slim image of the unit's track and product is read, so the page knows whether it is behind
export function offerNewest(unit, shelf) {
  const { device, settings, notice, file } = unit.state ?? {}
  if (!device) offered.delete(unit)
  if (!device || !settings || notice || file || offered.get(unit) === device || taking.has(unit)) return null
  const newest = fitting(shelf, unit).find(option => option.dataset.kind === "slim")
  if (!newest) return null
  offered.set(unit, device)
  return take(unit, newest)
}

export function taken(unit) {
  return taking.get(unit) ?? null
}

export async function take(unit, option, read = fetchImage) {
  const selection = new AbortController()
  taking.get(unit)?.controller.abort()
  const work = { controller: selection, option }
  taking.set(unit, work)
  unit.updater.clearImage()
  try {
    const bytes = await read(option.value, selection.signal)
    if (!selection.signal.aborted) await unit.updater.choose(bytes, option.value.split("/").pop())
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
