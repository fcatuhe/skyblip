import {
  FEET_PER_METRE, FPM_PER_METRE_SECOND, KNOTS_PER_METRE_SECOND, bearingDeg, distanceM, hasPosition, offset,
} from '#skyblip/geo';

export const LIVE_MS = 10_000;
export const DROPOUT_S = 3;

export class TrafficPicture {
  constructor(sourceNames, { referenceOrder = sourceNames } = {}) {
    this.sources = new Map(sourceNames.map(name => [name, { own: {}, status: {}, targets: new Map() }]));
    this.referenceOrder = referenceOrder;
    this.heardSeconds = new Map();
  }

  source(name) {
    return this.sources.get(name);
  }

  reference(at) {
    for (const name of this.referenceOrder) {
      const { own } = this.sources.get(name);
      if (hasPosition(own) && at - own.at <= LIVE_MS) return { source: name, ...own };
    }
    return null;
  }

  referencePressureFt(at) {
    for (const name of this.referenceOrder) {
      const { own } = this.sources.get(name);
      if (Number.isFinite(own.pressureAltitudeFt) && at - own.pressureAt <= LIVE_MS) return own.pressureAltitudeFt;
    }
    return null;
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
        placeOwn(own, message, at, ['groundSpeedKt', 'trackDeg', 'nic', 'nacp']);
        if (hasPosition(message)) pressOwn(own, message.pressureAltitudeFt, at);
        break;
      case 'pgrmz':
        pressOwn(own, message.pressureAltitudeFt, at);
        break;
      case 'ownship_altitude':
        own.geometricAltitudeFt = message.geometricAltitudeFt;
        break;
      case 'pflau':
      case 'heartbeat':
        Object.assign(source.status, message, { at });
        break;
      case 'pflaa':
        this.hear(source.targets, message, fromFlarm(own, message), at);
        break;
      case 'traffic':
        this.hear(source.targets, message, fromGdl90(this.reference(at), this.referencePressureFt(at), message), at);
        break;
    }
  }

  aircraft(at) {
    const rows = [...this.heardSeconds.keys()].map(address => this.rowOf(address, at));
    return rows.sort(byLiveThenNearest);
  }

  row(address, at) {
    return this.heardSeconds.has(address) ? this.rowOf(address, at) : null;
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

  hear(targets, message, report, at) {
    let target = targets.get(message.address);
    if (!target) {
      target = {
        address: message.address, callsign: null, firstAt: at, reports: 0, farthestM: 0, samples: [],
        dropouts: 0, longestGapS: 0, freshCount: 0, lastFreshAt: null, longestFreshGapS: 0, rssi: null,
      };
      targets.set(message.address, target);
    }
    const fresh = freshness(target.decoded, message);
    target.lastAt = at;
    target.reports += 1;
    target.callsign = message.callsign ?? target.callsign;
    target.report = { at, fresh, rssiDbm: message.rssiDbm ?? null, ...report };
    target.decoded = message;
    if (report.distanceM !== null) target.farthestM = Math.max(target.farthestM, report.distanceM);
    sample(target, at);
    if (!this.heardSeconds.has(message.address)) this.heardSeconds.set(message.address, new Set());
    this.heardSeconds.get(message.address).add(Math.floor(at / 1000));
  }

  rowOf(address, at) {
    const by = {};
    const signal = {};
    const heardS = this.heardSeconds.get(address).size;
    for (const [name, { targets }] of this.sources) {
      by[name] = targets.get(address) ?? null;
      signal[name] = by[name] ? signalOf(by[name], heardS) : null;
    }
    const heard = Object.entries(by).filter(([, target]) => target);
    const live = heard.filter(([, target]) => at - target.lastAt <= LIVE_MS).map(([name]) => name);
    const [firstName, first] = heard.reduce((best, entry) => (entry[1].firstAt < best[1].firstAt ? entry : best));
    const later = heard.filter(([name]) => name !== firstName).map(([, target]) => target.firstAt - first.firstAt);
    return {
      address,
      callsign: heard.map(([, target]) => target.callsign).find(Boolean) ?? null,
      by,
      signal,
      live,
      firstBy: firstName,
      leadMs: later.length > 0 ? Math.min(...later) : null,
      lastAt: Math.max(...heard.map(([, target]) => target.lastAt)),
      nearestM: Math.min(...heard.map(([, target]) => target.report.distanceM ?? Infinity)),
    };
  }
}

export function signalOf(target, heardS) {
  const spanMinutes = (target.lastAt - target.firstAt) / 60_000;
  const { rssi } = target;
  return {
    listedS: target.samples.length,
    coverage: heardS > 0 ? target.samples.length / heardS : null,
    dropouts: target.dropouts,
    longestGapS: target.longestGapS,
    freshCount: target.report.fresh === null ? null : target.freshCount,
    freshPerMinute: target.report.fresh === null || spanMinutes <= 0 ? null : target.freshCount / spanMinutes,
    longestFreshGapS: target.report.fresh === null ? null : target.longestFreshGapS,
    rssi: rssi && { last: rssi.last, mean: rssi.sum / rssi.count, min: rssi.min, max: rssi.max },
  };
}

function freshness(previous, message) {
  if (message.kind !== 'traffic') return null;
  if (!hasPosition(message)) return false;
  return !previous || previous.latitude !== message.latitude || previous.longitude !== message.longitude;
}

function sample(target, at) {
  const second = Math.floor(at / 1000);
  const { distanceM: distance, relativeVerticalFt, fresh, rssiDbm } = target.report;
  const last = target.samples.at(-1);
  if (last?.second === second) {
    Object.assign(last, { distanceM: distance, relativeVerticalFt, rssiDbm, fresh: last.fresh || fresh });
  } else {
    if (last) {
      const gap = second - last.second;
      if (gap > DROPOUT_S) target.dropouts += 1;
      target.longestGapS = Math.max(target.longestGapS, gap);
    }
    target.samples.push({ second, distanceM: distance, relativeVerticalFt, rssiDbm, fresh });
  }
  if (fresh) {
    if (target.lastFreshAt !== null) target.longestFreshGapS = Math.max(target.longestFreshGapS, (at - target.lastFreshAt) / 1000);
    target.lastFreshAt = at;
    target.freshCount += 1;
  }
  if (Number.isFinite(rssiDbm)) {
    const rssi = target.rssi ??= { count: 0, sum: 0, min: rssiDbm, max: rssiDbm };
    Object.assign(rssi, { count: rssi.count + 1, sum: rssi.sum + rssiDbm, last: rssiDbm });
    rssi.min = Math.min(rssi.min, rssiDbm);
    rssi.max = Math.max(rssi.max, rssiDbm);
  }
}

function placeOwn(own, message, at, fields) {
  if (!hasPosition(message)) return;
  own.latitude = message.latitude;
  own.longitude = message.longitude;
  own.at = at;
  for (const field of fields) if (message[field] !== null && message[field] !== undefined) own[field] = message[field];
}

function pressOwn(own, pressureAltitudeFt, at) {
  if (!Number.isFinite(pressureAltitudeFt)) return;
  own.pressureAltitudeFt = pressureAltitudeFt;
  own.pressureAt = at;
}

function fromFlarm(own, message) {
  const { northM, eastM } = message;
  const placed = northM !== null && eastM !== null;
  return {
    ...(placed && hasPosition(own) ? offset(own, northM, eastM) : {}),
    distanceM: placed ? Math.hypot(northM, eastM) : null,
    bearingDeg: placed ? (Math.atan2(eastM, northM) * 180 / Math.PI + 360) % 360 : null,
    relativeVerticalFt: message.verticalM === null ? null : message.verticalM * FEET_PER_METRE,
    altitudeFt: null,
    groundSpeedKt: message.groundSpeedMs === null ? null : message.groundSpeedMs * KNOTS_PER_METRE_SECOND,
    climbFpm: message.climbMs === null ? null : message.climbMs * FPM_PER_METRE_SECOND,
    trackDeg: message.trackDeg,
    alarmLevel: message.alarmLevel,
    category: message.aircraftType,
  };
}

function fromGdl90(reference, referencePressureFt, message) {
  const placed = hasPosition(message) && reference !== null;
  const stacked = message.pressureAltitudeFt !== null && referencePressureFt !== null;
  return {
    latitude: message.latitude,
    longitude: message.longitude,
    distanceM: placed ? distanceM(reference, message) : null,
    bearingDeg: placed ? bearingDeg(reference, message) : null,
    relativeVerticalFt: stacked ? message.pressureAltitudeFt - referencePressureFt : null,
    altitudeFt: message.pressureAltitudeFt,
    groundSpeedKt: message.groundSpeedKt,
    climbFpm: message.climbFpm,
    trackDeg: message.trackDeg,
    alarmLevel: message.alertStatus,
    category: message.emitterCategory,
  };
}

function byLiveThenNearest(a, b) {
  if ((a.live.length > 0) !== (b.live.length > 0)) return a.live.length > 0 ? -1 : 1;
  if (a.live.length > 0) return a.nearestM - b.nearestM || a.address.localeCompare(b.address);
  return b.lastAt - a.lastAt;
}
