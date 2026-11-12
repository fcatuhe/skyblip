const encoder = new TextEncoder();
const decoder = new TextDecoder();

export const FLARM_AIRCRAFT_TYPES = [
  'unknown', 'glider', 'tow plane', 'helicopter', 'skydiver', 'drop plane', 'hang glider', 'paraglider',
  'piston aircraft', 'jet aircraft', 'unknown', 'balloon', 'airship', 'UAV', 'reserved', 'static obstacle',
];

export const FLARM_SOURCES = { 0: 'FLARM', 1: 'ADS-B', 3: 'ADS-R', 4: 'TIS-B', 6: 'Mode-S' };

export function nmeaChecksum(body) {
  let sum = 0;
  for (const ch of encoder.encode(body)) sum ^= ch;
  return sum.toString(16).toUpperCase().padStart(2, '0');
}

export function nmeaLines(onLine) {
  let held = '';
  return bytes => {
    held += decoder.decode(bytes, { stream: true });
    const lines = held.split(/\r?\n/);
    held = lines.pop();
    for (const line of lines) if (line) onLine(line);
  };
}

export function parseNmea(line) {
  const match = /^\$(([A-Z0-9]+),?(.*))\*([0-9A-F]{2})$/i.exec(line.trim());
  if (!match || nmeaChecksum(match[1]) !== match[4].toUpperCase()) return null;
  const [, , name, body] = match;
  const parser = PARSERS[name] ?? PARSERS[name.slice(2)];
  return parser ? parser(body.split(',')) : { kind: 'other', sentence: name };
}

const PARSERS = {
  PFLAA: pflaa,
  PFLAU: pflau,
  PGRMZ: fields => ({ kind: 'pgrmz', pressureAltitudeFt: number(fields[0]), fix: number(fields[2]) }),
  RMC: rmc,
  GGA: gga,
};

function pflaa(fields) {
  const [id, callsign] = (fields[5] ?? '').split('!');
  const type = integer(fields[10], 16);
  return {
    kind: 'pflaa',
    alarmLevel: number(fields[0]),
    northM: number(fields[1]),
    eastM: number(fields[2]),
    verticalM: number(fields[3]),
    idType: number(fields[4]),
    address: id.toUpperCase().padStart(6, '0'),
    callsign: callsign?.trim() || null,
    trackDeg: number(fields[6]),
    turnRateDegS: number(fields[7]),
    groundSpeedMs: number(fields[8]),
    climbMs: number(fields[9]),
    aircraftType: type === null ? null : FLARM_AIRCRAFT_TYPES[type],
    noTrack: number(fields[11]),
    source: fields[12] ? FLARM_SOURCES[fields[12]] ?? fields[12] : null,
    rssiDbm: number(fields[13]),
  };
}

function pflau(fields) {
  return {
    kind: 'pflau',
    rx: number(fields[0]),
    tx: number(fields[1]),
    gps: number(fields[2]),
    power: number(fields[3]),
    alarmLevel: number(fields[4]),
    relativeBearingDeg: number(fields[5]),
    alarmType: number(fields[6]),
    relativeVerticalM: number(fields[7]),
    relativeDistanceM: number(fields[8]),
    address: fields[9] ? fields[9].toUpperCase() : null,
  };
}

function rmc(fields) {
  return {
    kind: 'rmc',
    time: fields[0] || null,
    valid: fields[1] === 'A',
    latitude: coordinate(fields[2], fields[3]),
    longitude: coordinate(fields[4], fields[5]),
    groundSpeedKt: number(fields[6]),
    trackDeg: number(fields[7]),
    date: fields[8] || null,
  };
}

function gga(fields) {
  return {
    kind: 'gga',
    time: fields[0] || null,
    latitude: coordinate(fields[1], fields[2]),
    longitude: coordinate(fields[3], fields[4]),
    quality: number(fields[5]),
    satellites: number(fields[6]),
    hdop: number(fields[7]),
    altitudeM: number(fields[8]),
    geoidSeparationM: number(fields[10]),
  };
}

function coordinate(value, hemisphere) {
  if (!value || !hemisphere) return null;
  const minutesAt = (value.includes('.') ? value.indexOf('.') : value.length) - 2;
  const unsigned = Number(value.slice(0, minutesAt)) + Number(value.slice(minutesAt)) / 60;
  return hemisphere === 'S' || hemisphere === 'W' ? -unsigned : unsigned;
}

function number(field) {
  if (field === undefined || field === '') return null;
  const value = Number(field);
  return Number.isNaN(value) ? null : value;
}

function integer(field, radix) {
  if (field === undefined || field === '') return null;
  const value = parseInt(field, radix);
  return Number.isNaN(value) ? null : value;
}
