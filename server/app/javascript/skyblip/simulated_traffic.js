import { KNOTS_PER_METRE_SECOND } from '#skyblip/geo';

export const EQUIPMENT = { adsl: 0, alptas: 1, adsb: 2 };

const NEARBY_RANGE_M = [800, 4800];
const NEARBY_HEIGHT_M = [-600, 600];
const CRUISE_KT = [90, 180];
const SLOW_KT = [35, 90];
const CRUISE_TURN_DPS_E1 = [-20, 20];
const THERMAL_TURN_DPS_E1 = [80, 150];
const CRUISE_CLIMB_MM_S = [-4000, 4000];
const STRONG_CLIMB_MM_S = [4000, 8000];
const EXCEPTIONAL_SHARE = 0.2;

const AIRLINER_RANGE_M = [8000, 30_000];
const AIRLINER_HEIGHT_M = [-600, 8500];
const AIRLINER_KT = [105, 460];

const ADDRESS_MAX = 0xffffff;
const CALLSIGN_PREFIXES = ['F-G', 'F-H', 'F-J', 'F-C', 'D-E', 'D-K', 'G-', 'HB-', 'OO-', 'EC-', 'I-'];
const REGISTRATION_LENGTH = 6;
const LETTERS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ';

export function nearbyAircraft(random = Math.random) {
  const exceptional = () => random() < EXCEPTIONAL_SHARE;
  return {
    ...placed(random, NEARBY_RANGE_M, NEARBY_HEIGHT_M),
    speedMps: metresPerSecond(between(random, exceptional() ? SLOW_KT : CRUISE_KT)),
    trackDeg: Math.round(random() * 359),
    turnDpsE1: exceptional() ? eitherHand(random, THERMAL_TURN_DPS_E1) : Math.round(between(random, CRUISE_TURN_DPS_E1)),
    climbMmS: exceptional() ? eitherHand(random, STRONG_CLIMB_MM_S) : Math.round(between(random, CRUISE_CLIMB_MM_S)),
    address: aircraftAddress(random),
  };
}

export function airliner(random = Math.random) {
  return {
    ...placed(random, AIRLINER_RANGE_M, AIRLINER_HEIGHT_M),
    speedMps: metresPerSecond(between(random, AIRLINER_KT)),
    trackDeg: Math.round(random() * 359),
    turnDpsE1: 0,
    climbMmS: 0,
    address: aircraftAddress(random),
  };
}

export function aircraftAddress(random = Math.random) {
  return 1 + Math.floor(random() * ADDRESS_MAX);
}

export function callsign(random = Math.random) {
  let text = CALLSIGN_PREFIXES[Math.floor(random() * CALLSIGN_PREFIXES.length)];
  while (text.length < REGISTRATION_LENGTH) text += LETTERS[Math.floor(random() * LETTERS.length)];
  return text;
}

export function addAircraft(sim, aircraft, equipment) {
  const { northM, eastM, upM, speedMps, trackDeg, turnDpsE1, climbMmS, address } = aircraft;
  return sim.addAircraft(northM, eastM, upM, speedMps, trackDeg, turnDpsE1, climbMmS, equipment, address);
}

export function metresPerSecond(knots) {
  return Math.round(knots / KNOTS_PER_METRE_SECOND);
}

function placed(random, rangeM, heightM) {
  const bearing = random() * 2 * Math.PI;
  const range = between(random, rangeM);
  return {
    northM: Math.round(Math.cos(bearing) * range),
    eastM: Math.round(Math.sin(bearing) * range),
    upM: Math.round(between(random, heightM)),
  };
}

function between(random, [low, high]) {
  return low + random() * (high - low);
}

function eitherHand(random, range) {
  const size = Math.round(between(random, range));
  return random() < 0.5 ? -size : size;
}
