import {
  FEET_PER_METRE, FPM_PER_METRE_SECOND, KNOTS_PER_METRE_SECOND, bearingDeg, distanceM, hasPosition, offset,
} from '#skyblip/geo';

export const LIVE_MS = 10_000;

export class TrafficPicture {
  constructor(sourceNames) {
    this.sources = new Map(sourceNames.map(name => [name, { own: {}, status: {}, targets: new Map() }]));
  }

  source(name) {
    return this.sources.get(name);
  }

  record(name, message, at) {
    const source = this.sources.get(name);
    const own = source.own;
    switch (message.kind) {
      case 'rmc':
        if (message.valid) placeOwn(own, message, at, ['groundSpeedKt', 'trackDeg']);
        break;
      case 'gga':
        if (message.quality > 0) placeOwn(own, message, at, ['altitudeM', 'satellites', 'hdop']);
        break;
      case 'ownship':
        placeOwn(own, message, at, ['pressureAltitudeFt', 'groundSpeedKt', 'trackDeg', 'nic', 'nacp']);
        break;
      case 'pgrmz':
        own.pressureAltitudeFt = message.pressureAltitudeFt;
        break;
      case 'ownship_altitude':
        own.geometricAltitudeFt = message.geometricAltitudeFt;
        break;
      case 'pflau':
      case 'heartbeat':
        Object.assign(source.status, message, { at });
        break;
      case 'pflaa':
        hear(source.targets, message, fromFlarm(own, message), at);
        break;
      case 'traffic':
        hear(source.targets, message, fromGdl90(own, message), at);
        break;
    }
  }

  aircraft(at) {
    const addresses = new Set();
    for (const { targets } of this.sources.values()) for (const address of targets.keys()) addresses.add(address);
    const rows = [...addresses].map(address => rowOf(this.sources, address, at));
    return rows.sort(byLiveThenNearest);
  }

  tally(at) {
    const names = [...this.sources.keys()];
    const rows = this.aircraft(at);
    const heard = row => names.filter(name => row.by[name]);
    return {
      total: rows.length,
      everyone: rows.filter(row => heard(row).length === names.length).length,
      only: Object.fromEntries(names.map(name => [name, rows.filter(row => heard(row).join() === name).length])),
      live: Object.fromEntries(names.map(name => [name, rows.filter(row => row.live.includes(name)).length])),
      farthestM: Object.fromEntries(names.map(name => [name, Math.max(0, ...rows.map(row => row.by[name]?.farthestM ?? 0))])),
    };
  }
}

function placeOwn(own, message, at, fields) {
  if (!hasPosition(message)) return;
  own.latitude = message.latitude;
  own.longitude = message.longitude;
  own.at = at;
  for (const field of fields) if (message[field] !== null && message[field] !== undefined) own[field] = message[field];
}

function fromFlarm(own, message) {
  const { northM, eastM } = message;
  const placed = northM !== null && eastM !== null;
  return {
    ...(placed && hasPosition(own) ? offset(own, northM, eastM) : {}),
    distanceM: placed ? Math.hypot(northM, eastM) : null,
    bearingDeg: placed ? (Math.atan2(eastM, northM) * 180 / Math.PI + 360) % 360 : null,
    relativeVerticalFt: message.verticalM === null ? null : message.verticalM * FEET_PER_METRE,
    groundSpeedKt: message.groundSpeedMs === null ? null : message.groundSpeedMs * KNOTS_PER_METRE_SECOND,
    climbFpm: message.climbMs === null ? null : message.climbMs * FPM_PER_METRE_SECOND,
    trackDeg: message.trackDeg,
    alarmLevel: message.alarmLevel,
  };
}

function fromGdl90(own, message) {
  const placed = hasPosition(message) && hasPosition(own);
  const stacked = message.pressureAltitudeFt !== null && Number.isFinite(own.pressureAltitudeFt);
  return {
    latitude: message.latitude,
    longitude: message.longitude,
    distanceM: placed ? distanceM(own, message) : null,
    bearingDeg: placed ? bearingDeg(own, message) : null,
    relativeVerticalFt: stacked ? message.pressureAltitudeFt - own.pressureAltitudeFt : null,
    altitudeFt: message.pressureAltitudeFt,
    groundSpeedKt: message.groundSpeedKt,
    climbFpm: message.climbFpm,
    trackDeg: message.trackDeg,
    alarmLevel: message.alertStatus,
  };
}

function hear(targets, message, report, at) {
  let target = targets.get(message.address);
  if (!target) {
    target = { address: message.address, callsign: null, firstAt: at, reports: 0, farthestM: 0 };
    targets.set(message.address, target);
  }
  target.lastAt = at;
  target.reports += 1;
  target.callsign = message.callsign ?? target.callsign;
  target.report = { at, ...report };
  target.decoded = message;
  if (report.distanceM !== null) target.farthestM = Math.max(target.farthestM, report.distanceM);
}

function rowOf(sources, address, at) {
  const by = {};
  for (const [name, { targets }] of sources) by[name] = targets.get(address) ?? null;
  const heard = Object.entries(by).filter(([, target]) => target);
  const live = heard.filter(([, target]) => at - target.lastAt <= LIVE_MS).map(([name]) => name);
  const [firstName, first] = heard.reduce((best, entry) => (entry[1].firstAt < best[1].firstAt ? entry : best));
  const later = heard.filter(([name]) => name !== firstName).map(([, target]) => target.firstAt - first.firstAt);
  const nearest = Math.min(...heard.map(([, target]) => target.report.distanceM ?? Infinity));
  return {
    address,
    callsign: heard.map(([, target]) => target.callsign).find(Boolean) ?? null,
    by,
    live,
    firstBy: firstName,
    leadMs: later.length > 0 ? Math.min(...later) : null,
    lastAt: Math.max(...heard.map(([, target]) => target.lastAt)),
    nearestM: nearest,
  };
}

function byLiveThenNearest(a, b) {
  if ((a.live.length > 0) !== (b.live.length > 0)) return a.live.length > 0 ? -1 : 1;
  if (a.live.length > 0) return a.nearestM - b.nearestM || a.address.localeCompare(b.address);
  return b.lastAt - a.lastAt;
}
