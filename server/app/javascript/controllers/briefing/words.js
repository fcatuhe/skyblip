let source = null
let parsed = {}

function words() {
  const json = document.body.dataset.briefingWords ?? "{}"
  if (json !== source) {
    source = json
    parsed = JSON.parse(json)
  }
  return parsed
}

export function say(path, values = {}) {
  const text = path.split(".").reduce((node, key) => node?.[key], words())
  if (typeof text !== "string") return null
  return text.replace(/%\{(\w+)\}/g, (_, key) => values[key] ?? "")
}

export function sayCount(path, count) {
  return say(`${path}.${count === 1 ? "one" : "other"}`, { count })
}

export function sayNotice({ key, detail }, ...scopes) {
  const text = scopes.map(scope => say(`${scope}.${key}`)).find(Boolean)
  if (text) return [text, detail].filter(Boolean).join(" ")
  return [say(`${scopes.at(-1)}.refused`), key, detail].filter(Boolean).join(" ")
}
