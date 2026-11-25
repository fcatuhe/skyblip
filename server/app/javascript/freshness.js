const DIGEST = "X-Assets-Digest"
const loaded = document.querySelector('meta[name="assets-digest"]')?.content

// INFO: fc 25nov26 a full load ends every Bluetooth link, so a page that holds one keeps its scripts and renders the new HTML
addEventListener("turbo:before-fetch-response", event => {
  const { fetchResponse } = event.detail
  if (event.target.closest?.("turbo-frame") || !fetchResponse.isHTML) return
  if (stale(fetchResponse.header(DIGEST)) && mayReload()) {
    event.preventDefault()
    location.assign(fetchResponse.location)
  }
})

document.addEventListener("visibilitychange", async () => {
  if (document.visibilityState !== "visible" || !refreshesInForeground() || !navigator.onLine) return
  try {
    const response = await fetch(location.href, { method: "HEAD", cache: "no-store" })
    if (stale(response.headers.get(DIGEST)) && mayReload()) location.reload()
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

function refreshesInForeground() {
  return document.querySelector('meta[name="assets-refresh"]')?.content === "foreground"
}
