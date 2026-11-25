import { Controller } from "@hotwired/stimulus"
import { FEET_PER_METRE, FPM_PER_METRE_SECOND } from "#skyblip/geo"
import { EQUIPMENT, addAircraft, aircraftAddress, metresPerSecond, nearbyAircraft } from "#skyblip/simulated_traffic"
import { AIR_EVENTS, drawBandChart } from "controllers/simulator/band_chart"

const MM_PER_METRE = 1000
const HPA_E1_TO_PA = 10
const RANGE_NM = [1, 2, 4, 8]
const MAX_STEP_MS = 100
const TAPE_LINES = 12
const MODES = ["dev", "demo", "training"]
const PRODUCT_NAMES = { go: "skyBlip\u00a0Go", bravo: "skyBlip\u00a0Bravo", paired: "skyBlip\u00a0Go paired with Bravo" }
const ACTIVATION_KEYS = [" ", "Enter"]
const LOADING_SCENARIOS = "Loading scenarios..."
const PLACED_TRAFFIC_RANGE_M = [800, 3800]
const PLACED_TRAFFIC_SPEED_MPS = 30
const WINGMAN_BEHIND_M = 130
const WINGMAN_RIGHT_M = 60

const sign = (value, zero = "") => (value > 0 ? "+" : value < 0 ? "" : zero)
const onOff = on => (on ? "ON" : "OFF")

const AXES = {
  speed: { flown: (sim, kt) => sim.setSpeed(kt), read: kt => `${kt} kt` },
  satellites: { flown: (sim, count) => sim.setSatellites(count), read: count => `${count} sats` },
  altitude: { flown: (sim, ft) => sim.setAltitude(Math.round(ft / FEET_PER_METRE)), read: ft => `${ft} ft` },
  track: { flown: (sim, deg) => sim.setTrack(deg), read: deg => `${String(deg).padStart(3, "0")}°` },
  turn: { flown: (sim, e1) => sim.setTurn(e1), read: e1 => `${sign(e1, "+")}${(e1 / 10).toFixed(1)} °/s` },
  slip: { flown: (sim, mg) => sim.setSlip(mg), read: mg => `${sign(mg)}${mg} mg` },
  climb: {
    flown: (sim, fpm) => sim.setClimb(Math.round((fpm * MM_PER_METRE) / FPM_PER_METRE_SECOND)),
    read: fpm => `${sign(fpm)}${fpm} fpm`
  },
  airmass: { flown: (sim, hpaE1) => sim.setAirmass(hpaE1 * HPA_E1_TO_PA), read: hpaE1 => `${(hpaE1 / 10).toFixed(1)} hPa` },
  range: { flown: (sim, step) => sim.setRange(step), read: step => `${RANGE_NM[step]} NM` },
  battery: { flown: (sim, mv) => sim.setBatteryMv(mv), read: mv => `${(mv / 1000).toFixed(2)} V` }
}

const FLIGHT_KEYS = {
  ArrowUp: ["climb", +1],
  ArrowDown: ["climb", -1],
  ArrowRight: ["turn", +1],
  ArrowLeft: ["turn", -1],
  f: ["speed", +1],
  s: ["speed", -1]
}

const READINGS = {
  fix: sim => (sim.fixValid() ? "3D" : "no fix"),
  satellites: sim => sim.satellites(),
  position: sim => `${(sim.latitude1e7() / 1e7).toFixed(5)}, ${(sim.longitude1e7() / 1e7).toFixed(5)}`,
  altitude: sim => `${(sim.altitudeHaeMm() / 1000).toFixed(1)} m`,
  speed: sim => `${(sim.speedMmS() / 1000).toFixed(2)} m/s`,
  track: sim => `${(sim.trackCdeg() / 100).toFixed(2).padStart(6, "0")}°`,
  turn: sim => `${sign(sim.turnCdps())}${(sim.turnCdps() / 100).toFixed(2)} °/s`,
  climb: sim => `${(sim.climbMmS() / 1000).toFixed(2)} m/s`,
  pressure: sim => `${(sim.pressureMpa() / 100000).toFixed(3)} hPa`,
  battery: sim => `${(sim.batteryMv() / 1000).toFixed(2)} V · ${sim.batteryPercent()} %${sim.batteryCharging() ? " charging" : ""}`,
  traffic: sim => `${sim.traffic()} target(s)`,
  formation: sim => `${sim.formationMembers()} with us`,
  rx: sim => `${sim.rxOk()} / ${sim.rxBad()}`,
  rx_wait: sim => `${sim.rxWait()} / ${sim.rxType()}`,
  rx_sync: sim => `${sim.rxUnframed()} / ${sim.rxMiskeyed()}`,
  tx: sim => `${sim.txOk()} / ${sim.txLost()}`,
  dwell: (sim, { DWELL }) => DWELL[sim.slotState()] || "-"
}

export default class extends Controller {
  static targets = ["error", "productName", "device", "glass", "lamp", "charge", "pageHint", "refreshState", "refreshCount",
                    "fixButton", "externalPowerButton", "devPanel", "phonePanel", "bandPanel", "scenario", "scenarioHint", "modeHint",
                    "equipment", "adsbOption", "bravoLamp", "bravoLampName", "bravoPaired", "bravoState", "bravoAlarm",
                    "bravoLinkRow", "bravoLinkButton", "phone", "adsbHint", "band", "tape", "reading", "alarm",
                    ...Object.keys(AXES).flatMap(axis => [axis, `${axis}Readout`])]
  static values = { src: String, manifest: String }

  async connect() {
    const session = this.session = new AbortController()
    try {
      const { load, ...labels } = await import(this.srcValue)
      if (session.signal.aborted) return
      const sim = await load()
      if (session.signal.aborted) return
      this.sim = sim
      this.labels = labels
      this.switches = { backlight: false, power: true, fix: true, externalPower: false }
      this.scenarios = new Map()
      this.startedAt = performance.now()
      for (const axis in AXES) this.#apply(axis)
      this.#listScenarios(session.signal).catch(error => {
        if (!session.signal.aborted) this.scenarioHintTarget.textContent = error.message
      })
      this.#applyMode()
      this.#run(session.signal)
    } catch (error) {
      if (!session.signal.aborted) this.errorTarget.textContent = error.message
    }
  }

  disconnect() {
    this.session.abort()
    cancelAnimationFrame(this.frame)
    this.sim = null
  }

  steer({ params: { axis } }) {
    if (this.sim) this.#apply(axis)
  }

  fly(event) {
    if (!this.sim || event.metaKey || event.ctrlKey || event.altKey) return
    if (event.target.isContentEditable || event.target.matches?.("input, textarea, select")) return
    const nudge = FLIGHT_KEYS[event.key] || FLIGHT_KEYS[event.key.toLowerCase()]
    if (!nudge) return
    event.preventDefault()
    const [axis, direction] = nudge
    const slider = this[`${axis}Target`]
    slider.value = Number(slider.value) + direction * Number(slider.step)
    this.#apply(axis)
  }

  press(event) {
    if (this.#accepts(event)) this.#hold(event, 1)
  }

  release(event) {
    if (this.#accepts(event)) this.#hold(event, 0)
  }

  reset() {
    location.reload()
  }

  toggleBacklight() {
    this.sim?.backlight(this.#flip("backlight"))
  }

  togglePower() {
    this.sim?.power(this.#flip("power"))
  }

  toggleFix() {
    if (!this.sim) return
    this.sim.setFix(this.#flip("fix"))
    this.fixButtonTarget.textContent = `Fix: ${onOff(this.switches.fix)}`
  }

  toggleExternalPower() {
    if (!this.sim) return
    this.sim.setExternalPower(this.#flip("externalPower"))
    this.externalPowerButtonTarget.textContent = `USB power: ${onOff(this.switches.externalPower)}`
  }

  toggleBravoLink() {
    this.sim?.setBravoLink(this.sim.bravoLinkUp() === 1 ? 0 : 1)
  }

  addTraffic() {
    if (this.sim) addAircraft(this.sim, nearbyAircraft(), this.#equipment())
  }

  addThreat() {
    this.sim?.addThreat(this.#equipment())
  }

  addTrafficAt({ params: { phase } }) {
    if (!this.sim) return
    const bearing = Math.random() * 2 * Math.PI
    const [near, far] = PLACED_TRAFFIC_RANGE_M
    const range = near + Math.random() * (far - near)
    this.sim.addAircraftAt(Math.round(Math.cos(bearing) * range), Math.round(Math.sin(bearing) * range), 0,
                           PLACED_TRAFFIC_SPEED_MPS, Math.round(Math.random() * 359), phase, -1, this.#equipment())
  }

  addWingman() {
    if (!this.sim) return
    const track = Number(this.trackTarget.value)
    const radians = (track * Math.PI) / 180
    this.sim.addAircraft(Math.round(-WINGMAN_BEHIND_M * Math.cos(radians) - WINGMAN_RIGHT_M * Math.sin(radians)),
                         Math.round(-WINGMAN_BEHIND_M * Math.sin(radians) + WINGMAN_RIGHT_M * Math.cos(radians)), 0,
                         metresPerSecond(Number(this.speedTarget.value)), track, 0, 0, EQUIPMENT.adsl, aircraftAddress())
  }

  clearTraffic() {
    this.sim?.clearTraffic()
  }

  loadScenario({ target: { value } }) {
    if (!this.sim) return
    if (!value) return location.reload()
    try {
      const scenario = this.scenarios.get(value)
      if (!scenario) throw new Error(`Scenario unavailable: ${value}`)
      const text = JSON.stringify(scenario)
      if (!this.sim.loadScenario(text, text.length)) throw new Error("The firmware rejected this scenario")
      this.#applyMode()
    } catch (error) {
      this.errorTarget.textContent = error.message
    }
  }

  #apply(axis) {
    const value = Number(this[`${axis}Target`].value)
    AXES[axis].flown(this.sim, value)
    this[`${axis}ReadoutTarget`].textContent = AXES[axis].read(value)
  }

  #flip(name) {
    this.switches[name] = !this.switches[name]
    return this.switches[name] ? 1 : 0
  }

  #accepts(event) {
    if (!this.sim) return false
    return !event.type.startsWith("key") || (!event.repeat && ACTIVATION_KEYS.includes(event.key))
  }

  #hold(event, down) {
    event.preventDefault()
    if (event.params.control === "pad") this.sim.holdPad(down)
    else this.sim.holdButton(down)
  }

  #equipment() {
    return EQUIPMENT[this.equipmentTarget.value]
  }

  #applyMode() {
    const mode = MODES[this.sim.mode()] || "dev"
    const product = this.labels.PRODUCT[this.sim.product()]
    this.element.dataset.mode = mode
    this.element.dataset.product = product
    for (const panel of this.devPanelTargets) panel.hidden = mode !== "dev"
    this.productNameTarget.textContent = PRODUCT_NAMES[product]
    this.phonePanelTarget.hidden = product === "go"
    this.deviceTarget.hidden = product === "bravo"
    if (product === "bravo") this.bandPanelTarget.hidden = true
    this.bravoPairedTarget.hidden = product !== "paired"
    this.bravoLinkRowTarget.hidden = mode !== "dev"
    this.adsbOptionTarget.hidden = product !== "paired"
    if (product !== "paired" && this.equipmentTarget.value === "adsb") this.equipmentTarget.value = "adsl"
    this.modeHintTarget.textContent = mode === "dev"
      ? "mode: dev, every control visible"
      : `mode: ${mode}, controls hidden, the scenario is driving`
  }

  async #listScenarios(signal) {
    const menu = this.scenarioTarget
    menu.disabled = true
    menu.replaceChildren(menu.options[0])
    if (!this.manifestValue) {
      this.scenarioHintTarget.textContent = "Scenarios are unavailable for this firmware release."
      return
    }
    this.scenarioHintTarget.textContent = LOADING_SCENARIOS
    const reply = await fetch(this.manifestValue, { signal })
    if (!reply.ok) throw new Error(`Scenario bundle unavailable: HTTP ${reply.status}`)
    const bundle = await reply.json()
    if (!bundle || typeof bundle !== "object" || Array.isArray(bundle)) throw new Error("Invalid scenario bundle")
    const entries = Object.entries(bundle)
    if (!entries.length) throw new Error("This firmware release contains no scenarios.")
    const options = entries.map(([file, scenario]) => {
      if (!scenario || typeof scenario !== "object" || Array.isArray(scenario)) throw new Error(`Invalid scenario: ${file}`)
      this.scenarios.set(file, scenario)
      return new Option(scenario.name || file, file)
    })
    menu.append(...options)
    menu.disabled = false
    this.scenarioHintTarget.textContent = ""
  }

  #run(signal) {
    const palette = getComputedStyle(this.element)
    const color = name => palette.getPropertyValue(`--${name}`).trim()
    const frame = () => {
      if (signal.aborted) return
      this.#draw(color)
      this.frame = requestAnimationFrame(frame)
    }
    this.frame = requestAnimationFrame(frame)
  }

  #draw(color) {
    const sim = this.sim
    sim.advance(Math.min(sim.elapsedMs() + MAX_STEP_MS, performance.now() - this.startedAt))
    if (this.element.dataset.product !== "go") this.#drawBravo()
    this.#drawGlass()
    for (const reading of this.readingTargets) reading.textContent = READINGS[reading.dataset.reading](sim, this.labels)
    drawBandChart(this.bandTarget, this.#bursts(), color)
    this.#drawTape()
    if (this.element.dataset.mode === "training") this.modeHintTarget.textContent = `mode: training, expectations failed: ${sim.failures()}`
    const level = sim.alarm()
    this.alarmTarget.textContent = `${this.labels.ALARM[level] || level}${sim.alarmDismissed() ? " (dismissed)" : ""}`
    this.alarmTarget.className = `alarm a${level}`
  }

  #drawGlass() {
    const { sim, labels } = this
    sim.paint(this.glassTarget)
    const refresh = sim.refreshing() ? (sim.refreshIsFull() ? 2 : 1) : 0
    const glass = this.glassTarget.classList
    glass.toggle("off", !sim.powered())
    glass.toggle("bl", Boolean(sim.backlightOn()))
    glass.toggle("refreshing", refresh > 0)
    glass.toggle("full", refresh === 2)
    this.refreshStateTarget.textContent = labels.REFRESH[refresh]
    this.refreshStateTarget.classList.toggle("full", refresh === 2)
    this.refreshCountTarget.textContent = `  ·  ${sim.presents()} refreshes`
    if (this.element.dataset.product !== "bravo") this.lampTarget.dataset.lamp = sim.lamp()
    this.chargeTarget.classList.toggle("sb-led--lit", Boolean(sim.batteryCharging()))
    this.pageHintTarget.textContent = [`page: ${labels.PAGES[sim.page()]}`, `backlight ${sim.backlightOn() ? "on" : "off"}`,
                                       `power ${sim.powered() ? "on" : "off"}`, labels.SHUTDOWN[sim.shutdownPhase()]].join("  ·  ")
  }

  #drawBravo() {
    const sim = this.sim
    const lines = this.#lastLines(sim.nmeaCount(), index => `${sim.nmeaLine(index)}\n`)
    this.phoneTarget.textContent = lines.length ? lines.join("") : "-"
    this.phoneTarget.scrollTop = this.phoneTarget.scrollHeight
    this.adsbHintTarget.textContent = `${sim.adsbHeard()} squitters heard, ${sim.adsbAircraft()} aircraft assembled.`
    const lamp = sim.bravoLamp()
    this.bravoLampTarget.dataset.lamp = lamp
    this.bravoLampNameTarget.textContent = lamp
    if (this.element.dataset.product !== "paired") return
    this.bravoStateTarget.textContent = this.labels.BRAVO_STATE[sim.bravoState()]
    this.bravoAlarmTarget.textContent = sim.bravoFollows() === 1 ? "Bravo plays the Go's calls." : "Bravo grades its own ADS-B."
    this.bravoLinkButtonTarget.textContent = `Link to the Go: ${sim.bravoLinkUp() === 1 ? "UP" : "LOST"}`
  }

  #bursts() {
    const sim = this.sim
    return Array.from({ length: sim.airCount() }, (_, index) => ({
      phaseMs: sim.airPhase(index), frequencyKhz: sim.airFrequencyKhz(index), event: AIR_EVENTS[sim.airEvent(index)]
    }))
  }

  #drawTape() {
    const sim = this.sim
    const lines = this.#lastLines(sim.airCount(), index =>
      Object.assign(document.createElement("span"), { className: AIR_EVENTS[sim.airEvent(index)], textContent: `${sim.airLine(index)}\n` }))
    this.tapeTarget.replaceChildren(...(lines.length ? lines : ["-"]))
  }

  #lastLines(count, line) {
    const lines = []
    for (let index = Math.max(0, count - TAPE_LINES); index < count; index++) lines.push(line(index))
    return lines
  }
}
