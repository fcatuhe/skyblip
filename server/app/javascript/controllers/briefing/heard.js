import { heardSettings } from "#skyblip/settings"
import { labelOf, optionOf, shownIn } from "controllers/briefing/fields"
import { say } from "controllers/briefing/words"

const SHOWN_IN = ["aircraft_type", "callsign", "aircraft_address"]

export function proposedIn(form, heard, edited) {
  const { elements } = form
  return Object.fromEntries(Object.entries(heardSettings(heard, edited)).filter(([key, value]) => offers(elements[key], value)))
}

export function heardView(form, heard, edited) {
  const proposed = proposedIn(form, heard, edited)
  const rows = SHOWN_IN.filter(key => key in proposed && proposed[key] !== edited[key])
    .map(key => [labelOf(form, key), shownIn(form, key, edited[key]), shownIn(form, key, proposed[key])])
  const type = heard.aircraft_type === 0 ? null : optionOf(form.elements.aircraft_type, heard.aircraft_type)?.text
  const aircraft = [type, heard.callsign, `ADS-B ${heard.aircraft_address}`].filter(Boolean).join(" · ")
  return { text: say(rows.length > 0 ? "words.heard" : "words.heard_same", { aircraft }), rows }
}

export function heardRow([setting, now, heard]) {
  const row = document.createElement("tr")
  const name = Object.assign(document.createElement("th"), { scope: "row", textContent: setting })
  row.append(name, ...[now, heard].map(text => Object.assign(document.createElement("td"), { textContent: text })))
  return row
}

function offers(field, value) {
  return Boolean(field) && !field.disabled && (field.tagName !== "SELECT" || Boolean(optionOf(field, value)))
}
