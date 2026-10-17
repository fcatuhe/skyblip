(async () => {
  const workbench = document.querySelector("[data-controller=dev-simulator][data-mode]")
  if (!workbench) throw new Error("Open /briefing/simulator and wait for the firmware workbench to boot")
  const menu = workbench.querySelector("#scenario")
  if (menu.disabled) throw new Error(workbench.querySelector("#scenariohint").textContent || "Scenarios are not ready")

  const url = new URL(workbench.dataset.devSimulatorManifestValue, location.href).href
  const downloads = performance.getEntriesByType("resource").filter(entry => entry.name === url)
  if (downloads.length !== 1) throw new Error(`Expected one scenario bundle download, got ${downloads.length}`)

  const response = await fetch(url)
  if (!response.ok) throw new Error(`Cannot inspect the published scenario bundle: HTTP ${response.status}`)
  const bundle = await response.json()
  const options = [...menu.options].filter(option => option.value)
  if (options.length !== Object.keys(bundle).length) throw new Error("The picker omitted published scenarios")
  for (const option of options) {
    if (option.textContent !== (bundle[option.value].name || option.value)) throw new Error(`Incorrect scenario label: ${option.value}`)
  }

  const selected = options.find(option => bundle[option.value].mode === "training") || options[0]
  menu.value = selected.value
  const resourcesBefore = performance.getEntriesByType("resource").length
  menu.dispatchEvent(new Event("change", { bubbles: true }))
  await Promise.resolve()
  await Promise.resolve()
  if (workbench.dataset.mode !== bundle[selected.value].mode) throw new Error("The selected scenario did not reach the firmware")
  if (performance.getEntriesByType("resource").length !== resourcesBefore) throw new Error("Selecting a scenario performed another download")
  const error = workbench.querySelector("[data-dev-simulator-target=error]").textContent
  if (error) throw new Error(error)
  return { scenarios: options.length, selected: selected.value, mode: workbench.dataset.mode }
})()
