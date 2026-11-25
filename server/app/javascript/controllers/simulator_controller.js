import { Controller } from "@hotwired/stimulus"
import { FEET_PER_METRE, FPM_PER_METRE_SECOND, KNOTS_PER_METRE_SECOND, degrees, radians } from "#skyblip/geo"
import { EQUIPMENT, addAircraft, airliner, callsign, nearbyAircraft } from "#skyblip/simulated_traffic"
import { movePart } from "controllers/simulator/gauge"

const MAX_CATCHUP_MS = 250
const ACTIVATION_KEYS = [" ", "Enter"]

const MM_PER_METRE = 1000
const GRAVITY_M_S2 = 9.80665
const signed = value => (Number(value) > 0 ? `+${value}` : `${value}`)
const clamp = (value, limit) => Math.min(Math.max(value, -limit), limit)

const AXES = {
  speed: {
    flown: (sim, kt) => sim.setSpeed(kt),
    read: kt => `${kt} kt`
  },
  altitude: {
    flown: (sim, ft) => sim.setAltitude(Math.round(ft / FEET_PER_METRE)),
    read: ft => `${ft} ft`
  },
  track: {
    flown: (sim, deg) => sim.setTrack(deg),
    read: deg => `${String(deg).padStart(3, "0")}°`
  },
  turn: {
    flown: (sim, e1) => sim.setTurn(e1),
    read: e1 => `${signed((e1 / 10).toFixed(1))} °/s`
  },
  slip: {
    flown: (sim, mg) => sim.setSlip(mg),
    read: mg => `${signed(mg)} mg`
  },
  climb: {
    flown: (sim, fpm) => sim.setClimb(Math.round((fpm * MM_PER_METRE) / FPM_PER_METRE_SECOND)),
    read: fpm => `${signed(fpm)} fpm`
  }
}

const FLIGHT_KEYS = {
  ArrowUp: ["climb", -1],
  ArrowDown: ["climb", +1],
  ArrowRight: ["turn", +1],
  ArrowLeft: ["turn", -1],
  f: ["speed", +1],
  s: ["speed", -1]
}

const TRAFFIC_KEY = "t"
const WAKE_HOLD_MS = 1000
const BOOT_DELAY_MS = 2000
const FIRST_TRAFFIC_MS = 3000
const TRAFFIC_MIN = 1
// INFO: fc 13nov26 the public page keeps the sky it shipped with, under the world's 32 | 18nov26 eight per group, so Bravo's ADS-B adds eight
const TRAFFIC_MAX_PER_GROUP = 8
const TRAFFIC_TICK_MIN_MS = 10_000
const TRAFFIC_TICK_MAX_MS = 30_000
const TRAFFIC_ARRIVAL_SHARE = 0.5
const TRAFFIC_STAY_MIN_MS = 60_000
const TRAFFIC_STAY_MAX_MS = 180_000
const TRAFFIC_NAMED_SHARE = 0.6
const FLARM_SHARE = 0.25

export default class extends Controller {
  static targets = ["canvas", "status", "pad", "lamp", "charge", "bravo", "bravoControl", "part", "attitude", "attitudeReadout",
                    ...Object.keys(AXES).flatMap(axis => [axis, `${axis}Readout`])]
  static values = { src: String, screens: Object }

  // INFO: fc 18nov26 a load superseded by another boot or a Turbo visit must not start the simulator
  #generation = 0

  connect() {
    this.bootTimer = setTimeout(() => this.start(), BOOT_DELAY_MS)
  }

  disconnect() {
    this.#generation++
    this.#stop()
    clearTimeout(this.bootTimer)
    clearTimeout(this.wakeTimer)
    this.sim = null
    this.element.classList.remove("simulator--running", "simulator--off")
  }

  async start() {
    clearTimeout(this.bootTimer)
    this.#stop()
    const generation = ++this.#generation
    const { load, PAGES } = await import(this.srcValue)
    if (generation !== this.#generation) return
    this.pages = PAGES
    const sim = await load()
    if (generation !== this.#generation) return
    this.bravoControlTarget.hidden = false
    this.paired = this.bravoTarget.checked
    if (this.paired) {
      const scenario = JSON.stringify({ product: "paired" })
      if (!sim.loadScenario(scenario, scenario.length)) throw new Error("The firmware rejected the paired scenario")
    }
    this.sim = sim
    this.departures = { nearby: new Map(), adsb: new Map() }
    this.nextTrafficMs = FIRST_TRAFFIC_MS
    this.element.classList.add("simulator--running")
    for (const axis in AXES) this.#apply(axis)
    this.#run()
  }

  #comeAndGo() {
    const now = this.sim.elapsedMs()
    if (now < this.nextTrafficMs) return
    this.nextTrafficMs = now + this.#between(TRAFFIC_TICK_MIN_MS, TRAFFIC_TICK_MAX_MS)
    this.#arrivals("nearby", now)
    if (this.paired) this.#arrivals("adsb", now)
  }

  #arrivals(group, now) {
    const departures = this.departures[group]
    for (const [index, due] of departures) {
      if (due <= now) {
        this.sim.removeAircraft(index)
        departures.delete(index)
      }
    }
    if (departures.size < TRAFFIC_MIN || Math.random() < TRAFFIC_ARRIVAL_SHARE)
      this.#addAircraft(group)
  }

  steer(event) {
    if (this.sim) this.#apply(event.target.dataset.axis)
  }

  fly(event) {
    if (!this.sim) return
    if (event.metaKey || event.ctrlKey || event.altKey) return
    if (event.key.toLowerCase() === TRAFFIC_KEY) {
      if (event.repeat) return
      event.preventDefault()
      this.addTraffic()
      return
    }
    const nudge = FLIGHT_KEYS[event.key] || FLIGHT_KEYS[event.key.toLowerCase()]
    if (!nudge) return
    event.preventDefault()
    const [axis, direction] = nudge
    const dial = this[`${axis}Target`]
    dial.value = Number(dial.value) + direction * Number(dial.step)
    this.#apply(axis)
  }

  #apply(axis) {
    const value = Number(this[`${axis}Target`].value)
    AXES[axis].flown(this.sim, value)
    this.#show(axis, value)
    this.#attitude()
  }

  #show(axis, value) {
    this[`${axis}ReadoutTarget`].textContent = AXES[axis].read(value)
    for (const part of this.partTargets) if (part.dataset.gaugeAxis === axis) movePart(part, value)
  }

  #attitude() {
    const speedMps = Number(this.speedTarget.value) / KNOTS_PER_METRE_SECOND
    const turnRadS = radians(Number(this.turnTarget.value) / 10)
    const climbMps = Number(this.climbTarget.value) / FPM_PER_METRE_SECOND
    const { pixelsPerDegree, pitchLimit, bankLimit } = this.attitudeTarget.dataset
    const bank = clamp(degrees(Math.atan2(turnRadS * speedMps, GRAVITY_M_S2)), Number(bankLimit))
    const pitch = clamp(degrees(Math.atan2(climbMps, speedMps)), Number(pitchLimit))
    this.attitudeTarget.setAttribute("transform", `translate(0, ${pitch * Number(pixelsPerDegree)}) rotate(${-bank})`)
    this.attitudeReadoutTarget.textContent = `${Math.round(Math.abs(bank))}\u00b0 ${bank < 0 ? "L" : "R"}`
  }

  restart() {
    this.sim = null
    this.element.classList.remove("simulator--off")
    this.start()
  }

  addTraffic() {
    if (!this.sim) return
    this.#addAircraft("nearby")
    if (this.paired) this.#addAircraft("adsb")
  }

  #addAircraft(group) {
    const departures = this.departures[group]
    if (departures.size >= TRAFFIC_MAX_PER_GROUP) return
    const index = group === "adsb"
      ? addAircraft(this.sim, airliner(), EQUIPMENT.adsb)
      : addAircraft(this.sim, nearbyAircraft(), Math.random() < FLARM_SHARE ? EQUIPMENT.alptas : EQUIPMENT.adsl)
    if (index < 0) return
    departures.set(index, this.sim.elapsedMs() + this.#between(TRAFFIC_STAY_MIN_MS, TRAFFIC_STAY_MAX_MS))
    if (Math.random() < TRAFFIC_NAMED_SHARE) this.sim.nameAircraft(index, callsign())
  }

  #between(low, high) {
    return low + Math.random() * (high - low)
  }

  hold(event) {
    if (this.#asleep()) {
      this.wakeTimer = setTimeout(() => this.restart(), WAKE_HOLD_MS)
      return
    }
    if (!this.#accepts(event)) return
    event.preventDefault()
    this.sim.holdButton(1)
  }

  release() {
    clearTimeout(this.wakeTimer)
    if (this.sim) this.sim.holdButton(0)
  }

  #asleep() {
    return !this.sim || this.sim.powered() === 0
  }

  touch(event) {
    if (!this.#accepts(event)) return
    event.preventDefault()
    this.padTarget.focus({ preventScroll: true })
    this.sim.holdPad(1)
  }

  lift() {
    if (this.sim) this.sim.holdPad(0)
  }

  #accepts(event) {
    if (!this.sim) return false
    if (event.type !== "keydown") return true
    return !event.repeat && ACTIVATION_KEYS.includes(event.key)
  }

  #run() {
    this.lastMs = performance.now()
    const frame = () => {
      this.#advance()
      this.#comeAndGo()
      this.#paint()
      this.timer = requestAnimationFrame(frame)
    }
    this.timer = requestAnimationFrame(frame)
  }

  #advance() {
    const now = performance.now()
    const elapsed = Math.min(now - this.lastMs, MAX_CATCHUP_MS)
    this.lastMs = now
    this.sim.advance(this.sim.elapsedMs() + elapsed)
  }

  #paint() {
    this.sim.paint(this.canvasTarget)
    this.#flownState()
    const powered = this.sim.powered() === 1
    this.element.classList.toggle("simulator--off", !powered)
    this.statusTarget.textContent = this.#screen()
    this.lampTarget.dataset.lamp = this.sim.lamp()
    this.chargeTarget.classList.toggle("sb-led--lit", this.sim.batteryCharging() === 1)
  }

  #flownState() {
    this.#show("altitude", Math.round((this.sim.altitudeHaeMm() * FEET_PER_METRE) / 10_000) * 10)
    this.#show("track", Math.round(this.sim.trackCdeg() / 100) % 360)
  }

  #screen() {
    const screen = this.sim.menuOpen() === 1 ? "menu" : this.pages[this.sim.page()]
    return this.screensValue[screen] ?? screen
  }

  #stop() {
    if (this.timer) cancelAnimationFrame(this.timer)
    this.timer = null
  }
}
