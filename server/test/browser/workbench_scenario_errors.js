(async () => {
  const workbench = document.querySelector("[data-controller=dev-simulator][data-mode]")
  if (!workbench) throw new Error("Open /briefing/simulator and wait for the firmware workbench to boot")
  const { mountSimulator } = await import("#skyblip/dev_simulator")
  const source = workbench.dataset.devSimulatorSrcValue
  const checks = [
    ["", "Scenarios are unavailable for this firmware release."],
    ["data:application/json,%7B%7D", "This firmware release contains no scenarios."],
    ["data:application/json,%5B%5D", "Invalid scenario bundle"],
    ["data:application/json,%7B%22broken.json%22%3Anull%7D", "Invalid scenario: broken.json"],
    [new URL("/missing-scenario-bundle.json", location.href).href, "Scenario bundle unavailable: HTTP 404"]
  ]
  for (const [manifest, expected] of checks) {
    const root = workbench.cloneNode(true)
    root.removeAttribute("data-controller")
    const session = new AbortController()
    document.body.append(root)
    try {
      await mountSimulator(root, source, manifest, session.signal)
      const hint = root.querySelector("#scenariohint")
      if (hint.textContent === "Loading scenarios...") {
        await new Promise(resolve => {
          const observer = new MutationObserver(() => {
            if (hint.textContent !== "Loading scenarios...") {
              observer.disconnect()
              resolve()
            }
          })
          observer.observe(hint, { childList: true, subtree: true, characterData: true })
        })
      }
      if (hint.textContent !== expected) throw new Error(`Expected '${expected}', got '${hint.textContent}'`)
      if (!root.querySelector("#scenario").disabled) throw new Error("An unavailable scenario picker remained enabled")
    } finally {
      session.abort()
      root.remove()
    }
  }
  return { unavailableScenarioCases: checks.length }
})()
