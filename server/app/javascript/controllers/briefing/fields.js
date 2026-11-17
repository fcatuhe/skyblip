import { say } from "controllers/briefing/words"

export function labelOf(form, key) {
  return form.querySelector(`label[for="${form.elements[key]?.id}"]`)?.textContent.trim()
}

export function shownIn(form, key, value) {
  const field = form.elements[key]
  if (field.type === "checkbox") return say(value ? "words.on" : "words.off")
  if (field.tagName === "SELECT") return optionOf(field, value)?.text ?? String(value)
  if (value === "") return say("words.none")
  return String(value)
}

export function optionOf(select, value) {
  return [...select.options].find(option => option.value === String(value))
}
