import "ahoy"

ahoy.configure({
  visitsUrl: "/hoy/visits",
  eventsUrl: "/hoy/events"
})

document.addEventListener("turbo:load", () => {
  ahoy.trackView()
})

ahoy.trackClicks("a, button, input[type=submit]")
ahoy.trackSubmits("form")
ahoy.trackChanges("input, textarea, select")

document.addEventListener("visibilitychange", () => {
  ahoy.track("$visibilitychange", { page: window.location.pathname, state: document.visibilityState })
})

window.addEventListener("pagehide", () => {
  ahoy.track("$pagehide", { page: window.location.pathname })
})
