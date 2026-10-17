These checks run against the real firmware workbench in a browser, without a bundler or mocked browser APIs.

Start Rails, then use an isolated agent-browser tab with the shared browser's CDP URL. Replace the origin below when Rails runs elsewhere.

```sh
CDP=$(agent-browser --session agent-browser get cdp-url)
agent-browser --session simulator-check --cdp "$CDP" --pin-tab open http://127.0.0.1:8119/briefing/simulator
agent-browser --session simulator-check --cdp "$CDP" wait '[data-controller=dev-simulator][data-mode=dev]'
agent-browser --session simulator-check --cdp "$CDP" eval --stdin < server/test/browser/workbench_keyboard.js
```

The keyboard check sends all six flight keys from body focus, verifies typing and modifier guards, then removes the workbench to verify document-listener cleanup. It throws on any failed assertion, including the regression where only a non-focusable root received key events.

Reopen the page before the scenario check because the keyboard check removes its workbench. The selected release must publish `scenarios.json` for this check.

```sh
agent-browser --session simulator-check --cdp "$CDP" open http://127.0.0.1:8119/briefing/simulator
agent-browser --session simulator-check --cdp "$CDP" wait '#scenario:not([disabled])'
agent-browser --session simulator-check --cdp "$CDP" eval --stdin < server/test/browser/workbench_scenarios.js
~/.pi/agent/git/github.com/fcatuhe/pi-wares/skills/agent-browser/leave.sh simulator-check
```

The scenario check verifies the published names, one initial bundle download, and a real firmware mode transition with no download when a scenario is selected. Its inspection fetch is separate from the client's initial download.
