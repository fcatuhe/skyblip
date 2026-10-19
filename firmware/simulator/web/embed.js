import createModule from './skyblip_simulator.js';

export const PAGES = ['radar', 'nearby', '6-pack', 'g meter', 'status', 'satellites', 'radio log', 'raw', 'capture', 'self test'];
export const ALARM = ['none', 'advisory'];
export const SHUTDOWN = ['running', 'parking', 'await release', 'off'];
export const REFRESH = ['idle', 'partial refresh', 'full refresh'];
export const DWELL = ['uplink RX · 869.525', 'retune O->M', 'slot 0 · 868.200',
                      'hop', 'slot 1 · 868.400', 'retune M->O'];

const SLOT_EDGE_STEP_MS = 5;
const REFUSAL_CEILING_MS = 10000;
const INK = [20, 20, 20];
const PAPER = [201, 201, 196];

export async function load(options = {}) {
  const { boot, ...moduleOptions } = options;
  const module = await createModule(moduleOptions);
  const call = (name, ret, args) => module.cwrap(name, ret, args);
  const noArgs = [];
  const oneNumber = ['number'];

  const sim = {
    setup: call('simulator_setup', null, noArgs),
    step: call('simulator_step', null, oneNumber),
    parkRefusal: call('simulator_park_refusal', 'number', oneNumber),
    loadScenario: call('simulator_load_scenario', 'number', ['string', 'number']),
    mode: call('simulator_mode', 'number', noArgs),
    failures: call('simulator_failures', 'number', noArgs),

    button: call('simulator_press_button', null, noArgs),
    holdButton: call('simulator_hold_button', null, oneNumber),
    holdPad: call('simulator_hold_pad', null, oneNumber),
    backlight: call('simulator_set_backlight', null, oneNumber),
    power: call('simulator_set_panel_power', null, oneNumber),

    setRange: call('simulator_set_range', null, oneNumber),
    setFix: call('simulator_set_fix', null, oneNumber),
    setPps: call('simulator_set_pps', null, oneNumber),
    setSats: call('simulator_set_satellites', null, oneNumber),
    setAlt: call('simulator_set_altitude_m', null, oneNumber),
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
    sendConfig: call('simulator_send_config', null, ['string']),
    prompt: call('simulator_prompt', 'number', noArgs),
    txNamed: call('simulator_tx_named', 'number', noArgs),
    rxNamed: call('simulator_rx_named', 'number', noArgs),
    nameAircraft: call('simulator_name_aircraft', null, ['number', 'string']),
    addThreat: call('simulator_add_threat', null, oneNumber),
    clearTraffic: call('simulator_clear_aircraft', null, noArgs),
    removeAircraft: call('simulator_remove_aircraft', null, oneNumber),
    aircraftCount: call('simulator_aircraft_count', 'number', noArgs),
    formationMembers: call('simulator_formation_members', 'number', noArgs),

    fb: call('simulator_panel', 'number', noArgs),
    W: call('simulator_panel_width', 'number', noArgs)(),
    H: call('simulator_panel_height', 'number', noArgs)(),
    STRIDE: call('simulator_panel_stride', 'number', noArgs)(),
    refreshing: call('simulator_panel_refreshing', 'number', noArgs),
    refreshIsFull: call('simulator_panel_refresh_is_full', 'number', noArgs),
    presents: call('simulator_present_count', 'number', noArgs),
    blOn: call('simulator_backlight_on', 'number', noArgs),
    powered: call('simulator_panel_powered', 'number', noArgs),
    page: call('simulator_page', 'number', noArgs),
    menuOpen: call('simulator_menu_open', 'number', noArgs),
    shutdownPhase: call('simulator_shutdown_phase', 'number', noArgs),

    fixValid: call('simulator_fix_valid', 'number', noArgs),
    sats: call('simulator_satellites', 'number', noArgs),
    lat: call('simulator_latitude_1e7', 'number', noArgs),
    lon: call('simulator_longitude_1e7', 'number', noArgs),
    altMm: call('simulator_altitude_hae_mm', 'number', noArgs),
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
    rxOk: call('simulator_rx_ok', 'number', noArgs),
    rxBad: call('simulator_rx_bad', 'number', noArgs),
    rxWait: call('simulator_rx_wait', 'number', noArgs),
    rxType: call('simulator_rx_type', 'number', noArgs),
    rxUnframed: call('simulator_rx_unframed', 'number', noArgs),
    rxMiskeyed: call('simulator_rx_miskeyed', 'number', noArgs),
    txOk: call('simulator_tx_ok', 'number', noArgs),
    txLost: call('simulator_tx_lost', 'number', noArgs),
    slotState: call('simulator_slot_state', 'number', noArgs),
    dwellFreqKhz: call('simulator_dwell_frequency_khz', 'number', noArgs),
    airCount: call('simulator_air_record_count', 'number', noArgs),
    airPhase: call('simulator_air_phase_ms', 'number', oneNumber),
    airEvent: call('simulator_air_event', 'number', oneNumber),
    airFreqKhz: call('simulator_air_frequency_khz', 'number', oneNumber),
    airLine: call('simulator_air_line', 'string', oneNumber),
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

  sim.paint = (canvas) => {
    const context = canvas.getContext('2d');
    if (image === null) image = context.createImageData(sim.W, sim.H);
    const base = sim.fb();
    const heap = module.HEAPU8;
    for (let y = 0; y < sim.H; y++) {
      for (let x = 0; x < sim.W; x++) {
        const black = heap[base + y * sim.STRIDE + (x >> 3)] & (0x80 >> (x & 7));
        const pixel = black ? INK : PAPER;
        const offset = (y * sim.W + x) * 4;
        image.data[offset] = pixel[0];
        image.data[offset + 1] = pixel[1];
        image.data[offset + 2] = pixel[2];
        image.data[offset + 3] = 255;
      }
    }
    context.putImageData(image, 0, 0);
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
