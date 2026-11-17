import { say } from "controllers/briefing/words"

export function browserRow({ id, label, self }, idle) {
  const row = document.createElement("li")
  const shown = label || say("browsers.unnamed")
  const name = Object.assign(document.createElement("span"), { textContent: shown })
  if (self) name.append(Object.assign(document.createElement("span"), { className: "is-self", textContent: ` · ${say("browsers.this_browser")}` }))
  const forget = Object.assign(document.createElement("button"), { type: "button", className: "button-app button-app--small", textContent: say("browsers.forget"), disabled: !idle })
  forget.setAttribute("aria-label", say("browsers.forget_named", { name: shown }))
  forget.dataset.action = "unit#forgetBrowser"
  forget.dataset.unitIdParam = id
  row.append(name, forget)
  return row
}
