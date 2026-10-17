(async () => {
  const workbench = document.querySelector("[data-controller=dev-simulator][data-mode=dev]")
  if (!workbench) throw new Error("Open /briefing/simulator and wait for the firmware workbench to boot")

  const press = (target, key, modifiers = {}) => {
    target.dispatchEvent(new KeyboardEvent("keydown", { key, bubbles: true, cancelable: true, ...modifiers }))
  }
  const slider = id => workbench.querySelector(`#${id}`)
  const expect = (condition, message) => { if (!condition) throw new Error(message) }

  document.activeElement.blur()
  for (const [key, id, direction] of [
    ["ArrowUp", "clb", 1], ["ArrowDown", "clb", -1],
    ["ArrowRight", "trn", 1], ["ArrowLeft", "trn", -1],
    ["f", "spd", 1], ["s", "spd", -1]
  ]) {
    const control = slider(id)
    const before = Number(control.value)
    press(document.body, key)
    expect(Number(control.value) === before + direction * Number(control.step), `${key} did not move ${id} from body focus`)
  }

  const before = slider("spd").value
  for (const field of [slider("spd"), document.createElement("textarea"), document.createElement("select"), document.createElement("div")]) {
    if (field.tagName === "DIV") field.contentEditable = "true"
    if (!field.isConnected) workbench.append(field)
    press(field, "f")
    expect(slider("spd").value === before, `Flight keys intercepted typing in ${field.tagName}`)
    if (field.id !== "spd") field.remove()
  }
  for (const modifier of ["ctrlKey", "metaKey", "altKey"]) {
    press(document.body, "f", { [modifier]: true })
    expect(slider("spd").value === before, `Flight keys intercepted ${modifier}`)
  }

  workbench.remove()
  await Promise.resolve()
  await Promise.resolve()
  press(document.body, "f")
  expect(slider("spd").value === before, "Disconnect left a document keyboard listener attached")
  return { keyboard: "passed", typing: "passed", disconnect: "passed" }
})()
