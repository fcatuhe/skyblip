const DIGEST = "X-Assets-Digest"
const loaded = document.querySelector('meta[name="assets-digest"]')?.content

// INFO: fc 25nov26 a full load ends every Bluetooth link, so a page that holds one keeps its scripts and renders the new HTML
addEventListener("turbo:before-fetch-response", event => {
  const { fetchResponse } = event.detail
  if (!fetchResponse.isHTML || !stale(fetchResponse.header(DIGEST))) return
  if (isPageVisit(event) && mayReload()) {
    event.preventDefault()
    location.assign(fetchResponse.location)
  } else if (isFrame(event) && mayReload()) {
    event.preventDefault()
    location.reload()
  }
})

document.addEventListener("visibilitychange", async () => {
  if (!inForeground() || !navigator.onLine) return
  const page = location.href
  try {
    const response = await fetch(page, { method: "HEAD", cache: "no-store" })
    if (location.href === page && inForeground() && stale(response.headers.get(DIGEST)) && mayReload()) location.reload()
  } catch (error) {
    console.warn("The page could not ask whether its scripts are current:", error.message)
  }
})

function stale(digest) {
  return Boolean(loaded && digest) && digest !== loaded
}

function mayReload() {
  return document.dispatchEvent(new CustomEvent("skyblip:before-reload", { cancelable: true }))
}

// INFO: fc 25nov26 Turbo sends a visit from the document, and a prefetch from its link, a form from its form and a frame from its frame
function isPageVisit(event) {
  return event.target === document.documentElement
}

function isFrame(event) {
  return event.target.localName === "turbo-frame"
}

function inForeground() {
  return document.visibilityState === "visible" && document.querySelector('meta[name="assets-refresh"]')?.content === "foreground"
}
