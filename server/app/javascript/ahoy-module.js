import "ahoy"

ahoy.configure({
  visitsUrl: "/hoy/visits",
  eventsUrl: "/hoy/events"
})

document.addEventListener("turbo:load", () => {
  ahoy.trackView()
})

ahoy.trackClicks("a, button, input[type=submit]")

// INFO: fc 19nov26 a middle click fires auxclick and never click, so trackClicks misses a link opened in a new tab that way
document.addEventListener("auxclick", ({ button, target }) => {
  const link = button === 1 && target.closest?.("a[href]")
  if (link) ahoy.track("$click", clickOn(link))
})

ahoy.trackSubmits("form")
ahoy.trackChanges("input, textarea, select")

document.addEventListener("visibilitychange", () => {
  ahoy.track("$visibilitychange", { page: window.location.pathname, state: document.visibilityState })
})

window.addEventListener("pagehide", () => {
  ahoy.track("$pagehide", { page: window.location.pathname })
})

function clickOn(link) {
  return {
    tag: "a",
    id: link.id || undefined,
    class: link.className || undefined,
    page: window.location.pathname,
    section: link.closest("[data-section]")?.dataset.section,
    text: link.textContent.replace(/\s+/g, " ").trim(),
    href: link.href
  }
}
