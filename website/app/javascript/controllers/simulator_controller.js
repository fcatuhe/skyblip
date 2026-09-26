import { Controller } from "@hotwired/stimulus"

const MAX_CATCHUP_MS = 250
const ACTIVATION_KEYS = [" ", "Enter"]

const TRAFFIC_MIN_RANGE_M = 800
const TRAFFIC_MAX_RANGE_M = 4800
const TRAFFIC_VERT_SPREAD_M = 600
const KT_TO_MPS = 0.514444
const TRAFFIC_MIN_CRUISE_KT = 90
const TRAFFIC_MAX_CRUISE_KT = 180
const TRAFFIC_MIN_SLOW_KT = 35
const TRAFFIC_CRUISE_TURN_DPS_E1 = 20
const TRAFFIC_THERMAL_MIN_TURN_DPS_E1 = 80
const TRAFFIC_THERMAL_MAX_TURN_DPS_E1 = 150
const TRAFFIC_CRUISE_CLIMB_MM_S = 4000
const TRAFFIC_STRONG_MIN_CLIMB_MM_S = 4000
const TRAFFIC_STRONG_MAX_CLIMB_MM_S = 8000
const TRAFFIC_EXCEPTIONAL_SHARE = 0.2
const TRAFFIC_ADDRESS_MAX = 0xffffff
const ADSL = 0

const FEET_PER_METRE = 3.28084
const FPM_PER_MPS = 196.85
const MM_PER_METRE = 1000
const signed = value => (Number(value) > 0 ? `+${value}` : `${value}`)
const clamp = (value, limit) => Math.min(Math.max(value, -limit), limit)
const degrees = radians => (radians * 180) / Math.PI

const ASI_ZERO_DEG = 180
const ASI_SPAN_DEG = 315
const ASI_FULL_KT = 175
const ALT_DEG_PER_HUNDRED_FT = 0.36
const ALT_DEG_PER_THOUSAND_FT = 0.036
const RATE_MARK_DEG = 20
const STANDARD_RATE_DPS = 3
const RATE_LIMIT_DEG = 45
const SLIP_FULL_MG = 200
const BALL_TRAVEL = 16
const VSI_ZERO_DEG = -90
const VSI_FULL_FPM = 2000
const VSI_KNEE_FPM = 1000
const VSI_KNEE_DEG = 90
const VSI_OUTER_DEG = 80

const vsiAngle = fpm => {
  const rate = Math.min(Math.abs(fpm), VSI_FULL_FPM)
  const inner = (Math.min(rate, VSI_KNEE_FPM) * VSI_KNEE_DEG) / VSI_KNEE_FPM
  const outer =
    (Math.max(rate - VSI_KNEE_FPM, 0) * VSI_OUTER_DEG) / (VSI_FULL_FPM - VSI_KNEE_FPM)
  return VSI_ZERO_DEG + Math.sign(fpm) * (inner + outer)
}
const BANK_LIMIT_DEG = 60
const PITCH_FULL_DEG = 20
const GAUGE_RADIUS = 49

const turned = (gauge, part, deg) =>
  gauge.querySelector(part).setAttribute("transform", `rotate(${deg})`)

const upright = (gauge, deg) => {
  for (const numeral of gauge.querySelectorAll(".gauge-numeral"))
    numeral.setAttribute(
      "transform", `rotate(${deg}, ${numeral.getAttribute("x")}, ${numeral.getAttribute("y")})`
    )
}

const AXES = {
  speed: {
    flown: (sim, kt) => sim.setSpeed(kt),
    read: kt => `${kt} kt`,
    paint: (gauge, kt) =>
      turned(gauge, ".gauge-needle",
             ASI_ZERO_DEG + (Math.min(kt, ASI_FULL_KT) * ASI_SPAN_DEG) / ASI_FULL_KT)
  },
  altitude: {
    flown: (sim, ft) => sim.setAlt(Math.round(ft / FEET_PER_METRE)),
    read: ft => `${ft} ft`,
    paint: (gauge, ft) => {
      turned(gauge, ".gauge-needle", (ft % 1000) * ALT_DEG_PER_HUNDRED_FT)
      turned(gauge, ".gauge-hand", (ft % 10000) * ALT_DEG_PER_THOUSAND_FT)
    }
  },
  track: {
    flown: (sim, deg) => sim.setTrack(deg),
    read: deg => `${String(deg).padStart(3, "0")}°`,
    paint: (gauge, deg) => {
      turned(gauge, ".gauge-card", -deg)
      upright(gauge, deg)
    }
  },
  turn: {
    flown: (sim, e1) => sim.setTurn(e1),
    read: e1 => `${signed((e1 / 10).toFixed(1))} °/s`,
    paint: (gauge, e1) =>
      turned(gauge, ".gauge-plane",
             clamp((e1 * RATE_MARK_DEG) / (10 * STANDARD_RATE_DPS), RATE_LIMIT_DEG))
  },
  slip: {
    flown: (sim, mg) => sim.setSlip(mg),
    read: mg => `${signed(mg)} mg`,
    paints: "turn",
    paint: (gauge, mg) => gauge.querySelector(".gauge-ball").setAttribute(
      "transform", `translate(${(clamp(mg, SLIP_FULL_MG) * BALL_TRAVEL) / SLIP_FULL_MG}, 0)`
    )
  },
  climb: {
    flown: (sim, fpm) => sim.setClimb(Math.round((fpm * MM_PER_METRE) / FPM_PER_MPS)),
    read: fpm => `${signed(fpm)} fpm`,
    paint: (gauge, fpm) => turned(gauge, ".gauge-needle", vsiAngle(fpm))
  }
}

const FLIGHT_KEYS = {
  ArrowUp: ["climb", +1],
  ArrowDown: ["climb", -1],
  ArrowRight: ["turn", +1],
  ArrowLeft: ["turn", -1],
  f: ["speed", +1],
  s: ["speed", -1]
}

const TRAFFIC_KEY = "t"
const WAKE_HOLD_MS = 1000
const BOOT_DELAY_MS = 2000
const FIRST_TRAFFIC_MS = 3000

export default class extends Controller {
  static targets = ["canvas", "status", "pad", "alarm", "charge",
                    "horizonGauge", "attitudeReadout",
                    ...Object.keys(AXES).flatMap(axis => [axis, `${axis}Readout`, `${axis}Gauge`])]
  static values = { src: String, on: String, off: String, menu: String }

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
    this.sim = sim
    this.element.classList.add("simulator--running")
    for (const axis in AXES) this.#apply(axis)
    this.#run()
    this.trafficTimer = setTimeout(() => this.addTraffic(), FIRST_TRAFFIC_MS)
  }

  steer(event) {
    this.#apply(event.target.dataset.axis)
  }

  fly(event) {
    if (!this.sim || event.target !== this.padTarget) return
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
    const { read, paint, paints } = AXES[axis]
    this[`${axis}ReadoutTarget`].textContent = read(value)
    paint(this[`${paints || axis}GaugeTarget`], value, this[`${axis}Target`])
  }

  #attitude() {
    const kt = Number(this.speedTarget.value)
    const dps = Number(this.turnTarget.value) / 10
    const fpm = Number(this.climbTarget.value)
    const bank = clamp(degrees(Math.atan2(dps * kt, 1093)), BANK_LIMIT_DEG)
    const pitch = clamp(degrees(Math.atan2(fpm * 10, kt * 1013)), PITCH_FULL_DEG)
    this.horizonGaugeTarget.querySelector(".gauge-attitude").setAttribute(
      "transform", `translate(0, ${(pitch * GAUGE_RADIUS) / PITCH_FULL_DEG}) rotate(${-bank})`
    )
    this.attitudeReadoutTarget.textContent = `${Math.round(Math.abs(bank))}\u00b0 ${bank < 0 ? "L" : "R"}`
  }

  restart() {
    this.sim = null
    this.element.classList.remove("simulator--off")
    this.start()
  }

  addTraffic() {
    if (!this.sim) return
    const bearing = Math.random() * 2 * Math.PI
    const range = this.#between(TRAFFIC_MIN_RANGE_M, TRAFFIC_MAX_RANGE_M)
    this.sim.addAircraft(
      Math.round(Math.cos(bearing) * range),
      Math.round(Math.sin(bearing) * range),
      Math.round(this.#between(-TRAFFIC_VERT_SPREAD_M, TRAFFIC_VERT_SPREAD_M)),
      this.#speedMps(),
      Math.round(Math.random() * 359),
      this.#turnDpsE1(),
      this.#climbMmS(),
      ADSL,
      this.#address()
    )
  }

  #address() {
    return 1 + Math.floor(Math.random() * TRAFFIC_ADDRESS_MAX)
  }

  #speedMps() {
    const kt = this.#exceptional()
      ? this.#between(TRAFFIC_MIN_SLOW_KT, TRAFFIC_MIN_CRUISE_KT)
      : this.#between(TRAFFIC_MIN_CRUISE_KT, TRAFFIC_MAX_CRUISE_KT)
    return Math.round(kt * KT_TO_MPS)
  }

  #turnDpsE1() {
    if (!this.#exceptional())
      return Math.round(this.#between(-TRAFFIC_CRUISE_TURN_DPS_E1, TRAFFIC_CRUISE_TURN_DPS_E1))
    return this.#eitherHand(TRAFFIC_THERMAL_MIN_TURN_DPS_E1, TRAFFIC_THERMAL_MAX_TURN_DPS_E1)
  }

  #climbMmS() {
    if (!this.#exceptional())
      return Math.round(this.#between(-TRAFFIC_CRUISE_CLIMB_MM_S, TRAFFIC_CRUISE_CLIMB_MM_S))
    return this.#eitherHand(TRAFFIC_STRONG_MIN_CLIMB_MM_S, TRAFFIC_STRONG_MAX_CLIMB_MM_S)
  }

  #exceptional() {
    return Math.random() < TRAFFIC_EXCEPTIONAL_SHARE
  }

  #eitherHand(low, high) {
    const size = this.#between(low, high)
    return Math.round(Math.random() < 0.5 ? -size : size)
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
    this.statusTarget.textContent = `${this.#screen()} · ${powered ? this.onValue : this.offValue}`
    this.alarmTarget.classList.toggle("sb-led--lit", this.sim.alarm() > 0)
    this.chargeTarget.classList.toggle("sb-led--lit", this.sim.batteryCharging() === 1)
  }

  #flownState() {
    this.#show("altitude", Math.round((this.sim.altMm() * FEET_PER_METRE) / 10_000) * 10)
    this.#show("track", Math.round(this.sim.trackCdeg() / 100) % 360)
  }

  #screen() {
    return this.sim.menuOpen() === 1 ? this.menuValue : this.pages[this.sim.page()]
  }

  #stop() {
    clearTimeout(this.trafficTimer)
    if (this.timer) cancelAnimationFrame(this.timer)
    this.timer = null
  }
}
