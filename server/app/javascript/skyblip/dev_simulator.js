export async function mountSimulator(element, src, manifest, signal) {
  const { load, PAGES, ALARM, DWELL, SHUTDOWN, REFRESH } = await import(src);

  const SIM = {};

  const f = await load();
  if (signal.aborted) return;
  let frame;
  const startedAt = performance.now();

  let bl = false, pwr = true, fix = true, ext = false;

  const $ = id => element.querySelector(`#${id}`);
  const canvas = $('epd');

  SIM.button = () => f.button();
  SIM.hold = down => f.holdButton(down);
  SIM.pad = down => f.holdPad(down);
  SIM.reset = () => location.reload();
  SIM.backlight = () => { bl = !bl; f.backlight(bl ? 1 : 0); };
  SIM.power = () => { pwr = !pwr; f.power(pwr ? 1 : 0); };
  SIM.toggleFix = () => { fix = !fix; f.setFix(fix ? 1 : 0);
                          $('fixbtn').textContent = 'Fix: ' + (fix ? 'ON' : 'OFF'); };
  SIM.sats  = v => { f.setSatellites(+v);  $('sats_o').textContent = v + ' sats'; };
  const FEET_PER_METRE = 3.28084;
  const FPM_PER_MM_S = 0.19685;
  SIM.alt   = v => { f.setAltitude(Math.round(v / FEET_PER_METRE));
                     $('alt_o').textContent = v + ' ft'; };
  SIM.speed = v => { f.setSpeed(+v); $('spd_o').textContent = v + ' kt'; };
  SIM.track = v => { f.setTrack(+v); $('trk_o').textContent = String(v).padStart(3,'0') + '°'; };
  SIM.turn  = v => { f.setTurn(+v);
                     $('trn_o').textContent = (v >= 0 ? '+' : '') + (v/10).toFixed(1) + ' °/s'; };
  SIM.slip  = v => { f.setSlip(+v);
                     $('slp_o').textContent = (v > 0 ? '+' : '') + v + ' mg'; };
  SIM.climb = v => { f.setClimb(Math.round(v / FPM_PER_MM_S));
                     $('clb_o').textContent = (v > 0 ? '+' : '') + v + ' fpm'; };
  const HPA_E1_TO_PA = 10;
  SIM.airmass = v => { f.setAirmass(v * HPA_E1_TO_PA); $('air_o').textContent = (v/10).toFixed(1) + ' hPa'; };
  const RANGE_NM = [1, 2, 4, 8];
  SIM.range = v => { f.setRange(+v); $('rng_o').textContent = RANGE_NM[+v] + ' NM'; };
  SIM.battery = v => { f.setBatteryMv(+v); $('bat_o').textContent = (v/1000).toFixed(2) + ' V'; };
  SIM.toggleExternalPower = () => { ext = !ext; f.setExternalPower(ext ? 1 : 0);
                                    $('extbtn').textContent = 'USB power: ' + (ext ? 'ON' : 'OFF'); };
  const alptas = () => ($('tfc_system').value === 'alptas' ? 1 : 0);
  const KT_TO_MPS = 0.514444;
  const TRAFFIC_CRUISE_KT = [90, 180];
  const TRAFFIC_SLOW_KT = [35, 90];
  const TRAFFIC_VERT_SPREAD_M = 600;
  const TRAFFIC_ADDRESS_MAX = 0xffffff;
  const TRAFFIC_CRUISE_TURN_DPS_E1 = 20;
  const TRAFFIC_THERMAL_TURN_DPS_E1 = [80, 150];
  const TRAFFIC_CRUISE_CLIMB_MM_S = 4000;
  const TRAFFIC_STRONG_CLIMB_MM_S = [4000, 8000];
  const TRAFFIC_EXCEPTIONAL_SHARE = 0.2;
  const spread = limit => Math.round((Math.random() * 2 - 1) * limit);
  const within = ([low, high]) => low + Math.random() * (high - low);
  const exceptional = () => Math.random() < TRAFFIC_EXCEPTIONAL_SHARE;
  const eitherHand = range => Math.round(within(range)) * (Math.random() < 0.5 ? -1 : 1);
  const trafficSpeedMps = () =>
    Math.round(within(exceptional() ? TRAFFIC_SLOW_KT : TRAFFIC_CRUISE_KT) * KT_TO_MPS);
  const trafficTurnDpsE1 = () =>
    exceptional() ? eitherHand(TRAFFIC_THERMAL_TURN_DPS_E1) : spread(TRAFFIC_CRUISE_TURN_DPS_E1);
  const trafficClimbMmS = () =>
    exceptional() ? eitherHand(TRAFFIC_STRONG_CLIMB_MM_S) : spread(TRAFFIC_CRUISE_CLIMB_MM_S);
  const trafficAddress = () => 1 + Math.floor(Math.random() * TRAFFIC_ADDRESS_MAX);
  SIM.addRandom = () => {
    const brg = Math.random() * 2 * Math.PI, d = 800 + Math.random() * 4000;
    f.addAircraft(Math.round(Math.cos(brg)*d), Math.round(Math.sin(brg)*d),
                  spread(TRAFFIC_VERT_SPREAD_M),
                  trafficSpeedMps(), Math.round(Math.random()*359),
                  trafficTurnDpsE1(), trafficClimbMmS(), alptas(), trafficAddress());
  };
  SIM.addThreat = () => f.addThreat(alptas());
  SIM.addAt = phase => {
    const brg = Math.random() * 2 * Math.PI, d = 800 + Math.random() * 3000;
    f.addAircraftAt(Math.round(Math.cos(brg)*d), Math.round(Math.sin(brg)*d), 0, 30,
                    Math.round(Math.random()*359), phase, -1, alptas());
  };
  const MODES = ['dev', 'demo', 'training'];
  SIM.applyMode = () => {
    const mode = MODES[f.mode()] || 'dev';
    element.dataset.mode = mode;
    for (const panel of element.querySelectorAll('.panel')) {
      if (panel.id === 'scenariopanel') continue;
      panel.hidden = mode !== 'dev';
    }
    $('modehint').textContent = mode === 'dev'
      ? 'mode: dev, every control visible'
      : 'mode: ' + mode + ', controls hidden, the scenario is driving';
  };
  const scenarios = new Map();
  SIM.loadScenario = key => {
    if (!key) { location.reload(); return; }
    const scenario = scenarios.get(key);
    if (!scenario) throw new Error(`Scenario unavailable: ${key}`);
    const text = JSON.stringify(scenario);
    if (!f.loadScenario(text, text.length)) throw new Error('The firmware rejected this scenario');
    SIM.applyMode();
  };

  SIM.listScenarios = async () => {
    const menu = $('scenario');
    menu.disabled = true;
    menu.replaceChildren(menu.options[0]);
    if (!manifest) {
      $('scenariohint').textContent = 'Scenarios are unavailable for this firmware release.';
      return;
    }
    $('scenariohint').textContent = 'Loading scenarios...';
    const reply = await fetch(manifest, { signal });
    if (!reply.ok) throw new Error(`Scenario bundle unavailable: HTTP ${reply.status}`);
    const bundle = await reply.json();
    if (!bundle || typeof bundle !== 'object' || Array.isArray(bundle)) throw new Error('Invalid scenario bundle');
    const entries = Object.entries(bundle);
    if (!entries.length) throw new Error('This firmware release contains no scenarios.');
    const options = entries.map(([file, scenario]) => {
      if (!scenario || typeof scenario !== 'object' || Array.isArray(scenario)) throw new Error(`Invalid scenario: ${file}`);
      const option = document.createElement('option');
      option.value = file;
      option.textContent = scenario.name || file;
      scenarios.set(file, scenario);
      return option;
    });
    menu.append(...options);
    menu.disabled = false;
    $('scenariohint').textContent = '';
  };
  for (const [id, apply] of [['alt', SIM.alt], ['spd', SIM.speed], ['trk', SIM.track],
                             ['trn', SIM.turn], ['clb', SIM.climb], ['rng', SIM.range],
                             ['slp', SIM.slip],
                             ['sats', SIM.sats], ['bat', SIM.battery], ['air', SIM.airmass]])
    apply($(id).value);

  SIM.listScenarios().catch(error => {
    if (!signal.aborted) $('scenariohint').textContent = error.message;
  });
  SIM.applyMode();
  SIM.clearTraffic = () => f.clearTraffic();
  SIM.addWingman = () => {
    const kt = +$('spd').value, track = +$('trk').value;
    const rad = track * Math.PI / 180, ahead = -130, right = 60;
    f.addAircraft(Math.round(ahead * Math.cos(rad) - right * Math.sin(rad)),
                  Math.round(ahead * Math.sin(rad) + right * Math.cos(rad)), 0,
                  Math.round(kt * KT_TO_MPS), track, 0, 0, 0, trafficAddress());
  };

  const STICK = { ArrowUp:    ['clb', +1, SIM.climb],
                  ArrowDown:  ['clb', -1, SIM.climb],
                  ArrowRight: ['trn', +1, SIM.turn],
                  ArrowLeft:  ['trn', -1, SIM.turn],
                  f:          ['spd', +1, SIM.speed],
                  s:          ['spd', -1, SIM.speed] };
  document.addEventListener('keydown', event => {
    const stick = STICK[event.key] || STICK[event.key.toLowerCase()];
    if (!stick || event.metaKey || event.ctrlKey || event.altKey) return;
    if (event.target.isContentEditable || event.target.matches?.('input, textarea, select')) return;
    event.preventDefault();
    const [id, direction, apply] = stick, slider = $(id);
    slider.value = +slider.value + direction * +slider.step;
    apply(slider.value);
  }, { signal });

  const bandCanvas = $('band'), band = bandCanvas.getContext('2d');
  const EVENT = ['tx', 'rx', 'deaf', 'coll'];
  const palette = getComputedStyle(element);
  const color = name => palette.getPropertyValue(`--${name}`).trim();
  const EVENT_COLOR = {tx:color('accent'), rx:color('radio-rx'), deaf:color('radio-deaf'), coll:color('radio-collision')};

  function drawBand() {
    const W = bandCanvas.width, H = bandCanvas.height, ms = x => x * W / 1000;
    band.clearRect(0, 0, W, H);
    band.fillStyle = color('band-slot-one'); band.fillRect(0, 0, ms(200), 30);
    band.fillStyle = color('band-uplink'); band.fillRect(ms(205), 0, ms(190), 30);
    band.fillStyle = color('band-slot-zero'); band.fillRect(ms(400), 0, ms(400), 30);
    band.fillStyle = color('band-slot-one'); band.fillRect(ms(800), 0, ms(200), 30);
    band.fillStyle = color('band-text'); band.font = '12px system-ui';
    band.fillText('slot 1 cont., RX only', 6, 19);
    band.fillText('uplink 205-395, O-band', ms(208), 19);
    band.fillText('slot 0: 868.200', ms(408), 19);
    band.fillText('slot 1: 868.400 ->', ms(808), 19);
    band.strokeStyle = color('band-divider');
    for (const t of [200, 205, 395, 400, 800]) {
      band.beginPath(); band.moveTo(ms(t), 0); band.lineTo(ms(t), 96); band.stroke();
    }
    band.strokeStyle = color('band-window'); band.setLineDash([3, 3]);
    band.beginPath(); band.moveTo(ms(450), 0); band.lineTo(ms(450), 96);
    band.moveTo(ms(1000) - 1, 0); band.lineTo(ms(1000) - 1, 96); band.stroke();
    band.setLineDash([]);
    const LANE = {869525: 44, 868200: 64, 868400: 84};
    band.fillStyle = color('band-label'); band.font = '10px ui-monospace,monospace';
    band.fillText('869.525', 8, 42); band.fillText('868.200', 8, 62); band.fillText('868.400', 8, 82);
    const n = f.airCount();
    for (let i = 0; i < n; i++) {
      const khz = f.airFrequencyKhz(i);
      const y = LANE[khz > 869000 ? 869525 : (khz > 868300 ? 868400 : 868200)];
      band.fillStyle = EVENT_COLOR[EVENT[f.airEvent(i)]];
      band.fillRect(ms(f.airPhase(i)) - 1, y - 10, 3, 11);
    }
  }

  function drawTape() {
    const n = f.airCount(), out = [];
    for (let i = Math.max(0, n - 12); i < n; i++) {
      const line = document.createElement('span');
      line.className = EVENT[f.airEvent(i)];
      line.textContent = f.airLine(i) + '\n';
      out.push(line);
    }
    $('tape').replaceChildren(...(out.length ? out : ['-']));
  }

  function draw() {
    f.advance(Math.min(f.elapsedMs() + 100, performance.now() - startedAt));
    f.paint(canvas);
    const refresh = f.refreshing() ? (f.refreshIsFull() ? 2 : 1) : 0;
    canvas.className = 'sb-glass ' + (f.powered() ? '' : 'off ') + (f.backlightOn() ? 'bl ' : '') +
        (refresh ? (refresh === 2 ? 'refreshing full' : 'refreshing') : '');
    $('refreshstate').textContent = REFRESH[refresh];
    $('refreshstate').className = refresh === 2 ? 'full' : '';
    $('refreshcount').textContent = '  ·  ' + f.presents() + ' refreshes';
    $('led_alarm').className = 'sb-led sb-led--alarm' + (f.alarm() ? ' sb-led--lit' : '');
    $('led_charge').className = 'sb-led sb-led--charge' + (f.batteryCharging() ? ' sb-led--lit' : '');
    $('pagehint').textContent = 'page: ' + PAGES[f.page()] +
        '  ·  backlight ' + (f.backlightOn() ? 'on' : 'off') +
        '  ·  power ' + (f.powered() ? 'on' : 'off') +
        '  ·  ' + SHUTDOWN[f.shutdownPhase()];

    $('t_fix').textContent  = f.fixValid() ? '3D' : 'no fix';
    $('t_sats').textContent = f.satellites();
    $('t_pos').textContent  = (f.latitude1e7()/1e7).toFixed(5) + ', ' + (f.longitude1e7()/1e7).toFixed(5);
    $('t_alt').textContent  = (f.altitudeHaeMm()/1000).toFixed(1) + ' m';
    $('t_spd').textContent  = (f.speedMmS()/1000).toFixed(2) + ' m/s';
    $('t_trk').textContent  = (f.trackCdeg()/100).toFixed(2).padStart(6,'0') + '°';
    const turn = f.turnCdps()/100;
    $('t_turn').textContent = (turn > 0 ? '+' : '') + turn.toFixed(2) + ' °/s';
    $('t_vs').textContent   = (f.climbMmS()/1000).toFixed(2) + ' m/s';
    $('t_baro').textContent = (f.pressureMpa()/100000).toFixed(3) + ' hPa';
    $('t_bat').textContent  = (f.batteryMv()/1000).toFixed(2) + ' V · ' + f.batteryPercent() + ' %'
                              + (f.batteryCharging() ? ' charging' : '');
    $('t_tfc').textContent  = f.traffic() + ' target(s)';
    $('t_form').textContent = f.formationMembers() + ' with us';
    $('t_rx').textContent   = f.rxOk() + ' / ' + f.rxBad();
    $('t_rxwait').textContent = f.rxWait() + ' / ' + f.rxType();
    $('t_rxsync').textContent = f.rxUnframed() + ' / ' + f.rxMiskeyed();
    $('t_tx').textContent   = f.txOk() + ' / ' + f.txLost();
    $('t_dwell').textContent = DWELL[f.slotState()] || '-';
    drawBand();
    drawTape();
    if (element.dataset.mode === 'training')
      $('modehint').textContent = 'mode: training, expectations failed: ' + f.failures();
    const lvl = f.alarm();
    const el = $('t_alarm');
    const dismissed = f.alarmDismissed() ? ' (dismissed)' : '';
    el.textContent = (ALARM[lvl] || lvl) + dismissed;
    el.className = 'alarm a' + lvl;

    frame = requestAnimationFrame(draw);
  }
  frame = requestAnimationFrame(draw);

  signal.addEventListener('abort', () => cancelAnimationFrame(frame), { once: true });
  return SIM;
}
