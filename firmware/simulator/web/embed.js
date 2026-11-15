export const PAGES = ['radar', 'nearby', '6-pack', 'g meter', 'status', 'satellites', 'radio log', '', '', 'self test', 'bravo', 'bravo log'];
export const ALARM = ['none', 'advisory'];
export const PRODUCT = ['go', 'bravo', 'paired'];
export const BRAVO_STATE = ['unpaired', 'waiting', 'live', 'link down', 'receiver down', 'no UTC'];
export const SHUTDOWN = ['running', 'parking', 'await release', 'off'];
export const REFRESH = ['idle', 'partial refresh', 'full refresh'];
export const DWELL = ['uplink RX · 869.525', 'retune O->M', 'slot 0 · 868.200',
                      'hop', 'slot 1 · 868.400', 'retune M->O'];

const WASM = new URL('./skyblip_simulator.wasm', import.meta.url);
const SLOT_EDGE_STEP_MS = 5;
const REFUSAL_CEILING_MS = 10000;
const STDOUT = 1;
const STDERR = 2;
const INK = [20, 20, 20];
const PAPER = [201, 201, 196];

export async function load({ boot, wasm } = {}) {
  let memory = null;
  const { instance } = await WebAssembly.instantiate(wasm ?? await fetchWasm(), {
    wasi_snapshot_preview1: wasi(() => memory),
  });
  const exports = instance.exports;
  memory = exports.memory;
  exports._initialize();

  let view = new Uint8Array(memory.buffer);
  const heap = () => {
    if (view.buffer !== memory.buffer) view = new Uint8Array(memory.buffer);
    return view;
  };
  const toC = (text) => {
    if (text === null || text === undefined) return 0;
    const bytes = new TextEncoder().encode(String(text));
    const pointer = exports.malloc(bytes.length + 1);
    if (pointer === 0) throw new Error('the simulator ran out of memory');
    heap().set(bytes, pointer);
    heap()[pointer + bytes.length] = 0;
    return pointer;
  };
  const fromC = (pointer) => {
    if (pointer === 0) return '';
    const bytes = heap();
    const end = bytes.indexOf(0, pointer);
    return new TextDecoder().decode(bytes.subarray(pointer, end));
  };
  const call = (name, ret, args) => {
    const fn = exports[name];
    if (typeof fn !== 'function') throw new Error(`the simulator exports no ${name}`);
    return (...values) => {
      const passed = args.map((type, index) => (type === 'string' ? toC(values[index]) : values[index]));
      try {
        const result = fn(...passed);
        return ret === 'string' ? fromC(result) : result;
      } finally {
        args.forEach((type, index) => { if (type === 'string') exports.free(passed[index]); });
      }
    };
  };
  const noArgs = [];
  const oneNumber = ['number'];
  const sendConfigText = call('simulator_send_config', 'number', ['string']);

  const sim = {
    memory,
    setup: call('simulator_setup', null, noArgs),
    step: call('simulator_step', null, oneNumber),
    parkRefusal: call('simulator_park_refusal', 'number', oneNumber),
    loadScenario: call('simulator_load_scenario', 'number', ['string', 'number']),
    mode: call('simulator_mode', 'number', noArgs),
    product: call('simulator_product', 'number', noArgs),
    failures: call('simulator_failures', 'number', noArgs),

    button: call('simulator_press_button', null, noArgs),
    holdButton: call('simulator_hold_button', null, oneNumber),
    holdPad: call('simulator_hold_pad', null, oneNumber),
    backlight: call('simulator_set_backlight', null, oneNumber),
    power: call('simulator_set_panel_power', null, oneNumber),

    setRange: call('simulator_set_range', null, oneNumber),
    setFix: call('simulator_set_fix', null, oneNumber),
    setPps: call('simulator_set_pps', null, oneNumber),
    setSatellites: call('simulator_set_satellites', null, oneNumber),
    setAltitude: call('simulator_set_altitude_m', null, oneNumber),
    setSpeed: call('simulator_set_speed_kt', null, oneNumber),
    setTrack: call('simulator_set_track_deg', null, oneNumber),
    setClimb: call('simulator_set_climb_mm_s', null, oneNumber),
    setTurn: call('simulator_set_turn_dps_e1', null, oneNumber),
    setSlip: call('simulator_set_slip_mg', null, oneNumber),
    setAirmass: call('simulator_set_airmass_qnh_pa', null, oneNumber),
    setBatteryMv: call('simulator_set_battery_mv', null, oneNumber),
    setExternalPower: call('simulator_set_external_power', null, oneNumber),

    addAircraft: call('simulator_add_aircraft', 'number', Array(9).fill('number')),
    addAircraftAt: call('simulator_add_aircraft_at', null, Array(8).fill('number')),
    sendConfig: (command) => {
      if (sendConfigText(JSON.stringify(command)) !== 1) {
        throw new Error(`the link registry has no frame for ${JSON.stringify(command)}`);
      }
    },
    prompt: call('simulator_prompt', 'number', noArgs),
    txNamed: call('simulator_tx_named', 'number', noArgs),
    rxNamed: call('simulator_rx_named', 'number', noArgs),
    txHeldForB5: call('simulator_tx_held_for_b5', 'number', noArgs),
    rxStatus: call('simulator_rx_status', 'number', noArgs),
    uplinkStale: call('simulator_uplink_stale', 'number', noArgs),
    nameAircraft: call('simulator_name_aircraft', null, ['number', 'string']),
    addThreat: call('simulator_add_threat', null, oneNumber),
    clearTraffic: call('simulator_clear_aircraft', null, noArgs),
    removeAircraft: call('simulator_remove_aircraft', null, oneNumber),
    aircraftCount: call('simulator_aircraft_count', 'number', noArgs),
    formationMembers: call('simulator_formation_members', 'number', noArgs),

    panel: call('simulator_panel', 'number', noArgs),
    width: call('simulator_panel_width', 'number', noArgs)(),
    height: call('simulator_panel_height', 'number', noArgs)(),
    stride: call('simulator_panel_stride', 'number', noArgs)(),
    refreshing: call('simulator_panel_refreshing', 'number', noArgs),
    refreshIsFull: call('simulator_panel_refresh_is_full', 'number', noArgs),
    presents: call('simulator_present_count', 'number', noArgs),
    backlightOn: call('simulator_backlight_on', 'number', noArgs),
    powered: call('simulator_panel_powered', 'number', noArgs),
    page: call('simulator_page', 'number', noArgs),
    menuOpen: call('simulator_menu_open', 'number', noArgs),
    shutdownPhase: call('simulator_shutdown_phase', 'number', noArgs),

    fixValid: call('simulator_fix_valid', 'number', noArgs),
    satellites: call('simulator_satellites', 'number', noArgs),
    latitude1e7: call('simulator_latitude_1e7', 'number', noArgs),
    longitude1e7: call('simulator_longitude_1e7', 'number', noArgs),
    altitudeHaeMm: call('simulator_altitude_hae_mm', 'number', noArgs),
    speedMmS: call('simulator_speed_mm_s', 'number', noArgs),
    trackCdeg: call('simulator_track_cdeg', 'number', noArgs),
    climbMmS: call('simulator_climb_mm_s', 'number', noArgs),
    turnCdps: call('simulator_turn_cdps', 'number', noArgs),
    pressureMpa: call('simulator_pressure_mpa', 'number', noArgs),
    batteryMv: call('simulator_battery_mv', 'number', noArgs),
    batteryPercent: call('simulator_battery_percent', 'number', noArgs),
    batteryCharging: call('simulator_battery_charging', 'number', noArgs),

    traffic: call('simulator_traffic_count', 'number', noArgs),
    alarm: call('simulator_alarm_level', 'number', noArgs),
    alarmDismissed: call('simulator_alarm_dismissed', 'number', noArgs),
    lamp: call('simulator_lamp', 'string', noArgs),
    rxOk: call('simulator_rx_ok', 'number', noArgs),
    rxBad: call('simulator_rx_bad', 'number', noArgs),
    rxWait: call('simulator_rx_wait', 'number', noArgs),
    rxType: call('simulator_rx_type', 'number', noArgs),
    rxUnframed: call('simulator_rx_unframed', 'number', noArgs),
    rxMiskeyed: call('simulator_rx_miskeyed', 'number', noArgs),
    txOk: call('simulator_tx_ok', 'number', noArgs),
    txLost: call('simulator_tx_lost', 'number', noArgs),
    slotState: call('simulator_slot_state', 'number', noArgs),
    dwellFrequencyKhz: call('simulator_dwell_frequency_khz', 'number', noArgs),
    airCount: call('simulator_air_record_count', 'number', noArgs),
    airPhase: call('simulator_air_phase_ms', 'number', oneNumber),
    airEvent: call('simulator_air_event', 'number', oneNumber),
    airFrequencyKhz: call('simulator_air_frequency_khz', 'number', oneNumber),
    airLine: call('simulator_air_line', 'string', oneNumber),
    nmeaCount: call('simulator_nmea_count', 'number', noArgs),
    nmeaLine: call('simulator_nmea_line', 'string', oneNumber),
    adsbHeard: call('simulator_adsb_heard', 'number', noArgs),
    adsbAircraft: call('simulator_adsb_aircraft', 'number', noArgs),
    bravoLamp: call('simulator_bravo_lamp', 'string', noArgs),
    bravoState: call('simulator_bravo_state', 'number', noArgs),
    bravoSilent: call('simulator_bravo_silent', 'number', noArgs),
    setBravoLink: call('simulator_set_bravo_link', null, oneNumber),
    bravoLinkUp: call('simulator_bravo_link_up', 'number', noArgs),
  };

  let simMs = 0;
  let image = null;

  sim.elapsedMs = () => simMs;

  sim.advance = (targetMs) => {
    while (simMs + SLOT_EDGE_STEP_MS <= targetMs) {
      simMs += SLOT_EDGE_STEP_MS;
      sim.step(simMs);
    }
    return simMs;
  };

  const paintFrom = (canvas, base) => {
    const context = canvas.getContext('2d');
    if (image === null) image = context.createImageData(sim.width, sim.height);
    const pixels = heap();
    for (let y = 0; y < sim.height; y++) {
      for (let x = 0; x < sim.width; x++) {
        const black = pixels[base + y * sim.stride + (x >> 3)] & (0x80 >> (x & 7));
        const pixel = black ? INK : PAPER;
        const offset = (y * sim.width + x) * 4;
        image.data[offset] = pixel[0];
        image.data[offset + 1] = pixel[1];
        image.data[offset + 2] = pixel[2];
        image.data[offset + 3] = 255;
      }
    }
    context.putImageData(image, 0, 0);
  };

  sim.paint = (canvas) => paintFrom(canvas, sim.panel());

  // INFO: fc 14nov26 the briefing draws a phone's prompt with the Go's own page, not a copy of it
  const drawPrompt = call('simulator_draw_prompt', 'number', ['string', 'string', 'string', 'number']);
  const promptWords = call('simulator_prompt_words', 'string', noArgs);
  // INFO: fc 14nov26 null when the Go raises no such prompt, else what the page says in reading order
  sim.paintPrompt = (canvas, { prompt, argument = '', settings = '', seconds = 0 }) => {
    const base = drawPrompt(prompt, argument, settings, seconds);
    if (base === 0) return null;
    paintFrom(canvas, base);
    return promptWords();
  };

  sim.sleepAgain = () => {
    while (simMs <= REFUSAL_CEILING_MS) {
      if (sim.parkRefusal(simMs) === 1) return simMs;
      simMs += SLOT_EDGE_STEP_MS;
    }
    throw new Error('the refused boot never finished the frame it owed');
  };

  // INFO: fc 21sep26 the cell and the cable a refused boot reads are read in setup(), not after
  if (boot?.batteryMv !== undefined) sim.setBatteryMv(boot.batteryMv);
  if (boot?.externalPower !== undefined) sim.setExternalPower(boot.externalPower ? 1 : 0);
  sim.setup();
  return sim;
}

async function fetchWasm() {
  const response = await fetch(WASM);
  if (!response.ok) throw new Error(`${WASM} answered ${response.status}`);
  return response.arrayBuffer();
}

// INFO: fc 15nov26 only the WASI calls Zig's libc links in today, so a new one fails to instantiate rather than run
function wasi(memory) {
  const unexpected = (name) => () => { throw new Error(`the simulator called WASI ${name}, which embed.js does not provide`); };
  return {
    fd_write: (fd, iovs, count, written) => {
      if (fd !== STDOUT && fd !== STDERR) return unexpected(`fd_write to descriptor ${fd}`)();
      const words = new DataView(memory().buffer);
      let text = '';
      let bytes = 0;
      for (let index = 0; index < count; index++) {
        const base = words.getUint32(iovs + index * 8, true);
        const length = words.getUint32(iovs + index * 8 + 4, true);
        text += new TextDecoder().decode(new Uint8Array(memory().buffer, base, length));
        bytes += length;
      }
      (fd === STDERR ? console.error : console.log)(text);
      words.setUint32(written, bytes, true);
      return 0;
    },
    fd_seek: unexpected('fd_seek'),
    fd_close: unexpected('fd_close'),
  };
}
