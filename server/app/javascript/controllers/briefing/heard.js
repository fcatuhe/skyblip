import { heardSettings } from "#skyblip/settings"
import { say } from "controllers/briefing/words"

export function proposedIn(form, heard, edited) {
  const { elements } = form
  return Object.fromEntries(Object.entries(heardSettings(heard, edited)).filter(([key, value]) => offers(elements[key], value)))
}

export function heardLine(form, heard, edited) {
  const proposed = proposedIn(form, heard, edited)
  const same = Object.entries(proposed).every(([key, value]) => edited[key] === value)
  const type = "aircraft_type" in proposed ? optionOf(form.elements.aircraft_type, proposed.aircraft_type).text : null
  const aircraft = [heard.callsign, `ADS-B ${heard.aircraft_address}`, type].filter(Boolean).join(" · ")
  return { text: say(same ? "words.heard_same" : "words.heard", { aircraft }), same }
}

function offers(field, value) {
  return Boolean(field) && !field.disabled && (field.tagName !== "SELECT" || Boolean(optionOf(field, value)))
}

function optionOf(select, value) {
  return [...select.options].find(option => option.value === String(value))
}
