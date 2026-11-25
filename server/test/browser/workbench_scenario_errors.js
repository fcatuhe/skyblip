(async () => {
  const workbench = document.querySelector("[data-controller=dev-simulator][data-mode]")
  if (!workbench) throw new Error("Open /briefing/simulator and wait for the firmware workbench to boot")
  const checks = [
    ["", "Scenarios are unavailable for this firmware release."],
    ["data:application/json,%7B%7D", "This firmware release contains no scenarios."],
    ["data:application/json,%5B%5D", "Invalid scenario bundle"],
    ["data:application/json,%7B%22broken.json%22%3Anull%7D", "Invalid scenario: broken.json"],
    [new URL("/missing-scenario-bundle.json", location.href).href, "Scenario bundle unavailable: HTTP 404"]
  ]
  const settled = hint => hint.textContent && !["Loading scenarios...", "Scenarios load with the firmware workbench."].includes(hint.textContent)
  for (const [manifest, expected] of checks) {
    const root = workbench.cloneNode(true)
    root.removeAttribute("data-mode")
    root.dataset.devSimulatorManifestValue = manifest
    document.body.append(root)
    try {
      const hint = root.querySelector("[data-dev-simulator-target=scenarioHint]")
      if (!settled(hint)) {
        await new Promise(resolve => {
          const observer = new MutationObserver(() => {
            if (settled(hint)) {
              observer.disconnect()
              resolve()
            }
          })
          observer.observe(hint, { childList: true, subtree: true, characterData: true })
        })
      }
      if (hint.textContent !== expected) throw new Error(`Expected '${expected}', got '${hint.textContent}'`)
      if (!root.querySelector("[data-dev-simulator-target=scenario]").disabled) throw new Error("An unavailable scenario picker remained enabled")
    } finally {
      root.remove()
    }
  }
  return { unavailableScenarioCases: checks.length }
})()
