const FLAG = 0x7e;
const ESCAPE = 0x7d;

export const ADDRESS_TYPES = ['ADS-B ICAO', 'ADS-B self-assigned', 'TIS-B ICAO', 'TIS-B track file', 'surface vehicle', 'ground station'];
export const TRACK_TYPES = ['none', 'true track', 'magnetic heading', 'true heading'];
export const EMITTER_CATEGORIES = {
  0: 'no information', 1: 'light', 2: 'small', 3: 'large', 4: 'high vortex large', 5: 'heavy',
  6: 'highly manoeuvrable', 7: 'rotorcraft', 9: 'glider', 10: 'lighter than air', 11: 'parachutist',
  12: 'ultralight', 14: 'UAV', 15: 'space vehicle', 17: 'emergency vehicle', 18: 'service vehicle',
  19: 'point obstacle', 20: 'cluster obstacle', 21: 'line obstacle',
};

const CRC_TABLE = Uint16Array.from({ length: 256 }, (_, byte) => {
  let crc = byte << 8;
  for (let bit = 0; bit < 8; bit++) crc = ((crc << 1) ^ (crc & 0x8000 ? 0x1021 : 0)) & 0xffff;
  return crc;
});

export function gdl90Crc(bytes) {
  let crc = 0;
  for (const byte of bytes) crc = CRC_TABLE[crc >> 8] ^ ((crc << 8) & 0xffff) ^ byte;
  return crc;
}

export function gdl90Frames(onMessage, onBadFrame) {
  let frame = [];
  let escaped = false;
  return bytes => {
    for (const byte of bytes) {
      if (byte === FLAG) {
        if (frame.length > 0) deliver(Uint8Array.from(frame), onMessage, onBadFrame);
        frame = [];
        escaped = false;
      } else if (byte === ESCAPE) {
        escaped = true;
      } else {
        frame.push(escaped ? byte ^ 0x20 : byte);
        escaped = false;
      }
    }
  };
}

function deliver(frame, onMessage, onBadFrame) {
  const body = frame.subarray(0, -2);
  const crc = frame.length > 2 ? frame[frame.length - 2] | (frame[frame.length - 1] << 8) : -1;
  if (crc === gdl90Crc(body)) onMessage(decodeGdl90(body), body);
  else onBadFrame?.(frame);
}

export function decodeGdl90(message) {
  const decoder = DECODERS[message[0]];
  return decoder && message.length >= decoder.bytes ? decoder.decode(message) : { kind: 'other', id: message[0] };
}

const DECODERS = {
  0: { bytes: 7, decode: heartbeat },
  10: { bytes: 28, decode: message => ({ kind: 'ownship', ...report(message) }) },
  11: { bytes: 5, decode: ownshipAltitude },
  20: { bytes: 28, decode: message => ({ kind: 'traffic', ...report(message) }) },
};

function heartbeat(message) {
  const [, status1, status2, low, high] = message;
  return {
    kind: 'heartbeat',
    gpsValid: Boolean(status1 & 0x80),
    maintenanceRequired: Boolean(status1 & 0x40),
    batteryLow: Boolean(status1 & 0x08),
    utcValid: Boolean(status2 & 0x01),
    secondsOfDay: ((status2 & 0x80) << 9) | (high << 8) | low,
  };
}

function ownshipAltitude(message) {
  const merit = (message[3] << 8) | message[4];
  return {
    kind: 'ownship_altitude',
    geometricAltitudeFt: signed((message[1] << 8) | message[2], 16) * 5,
    verticalWarning: Boolean(merit & 0x8000),
    verticalFigureOfMeritM: (merit & 0x7fff) === 0x7fff ? null : merit & 0x7fff,
  };
}

function report(message) {
  const altitude = (message[11] << 4) | (message[12] >> 4);
  const misc = message[12] & 0x0f;
  const horizontal = (message[14] << 4) | (message[15] >> 4);
  const vertical = ((message[15] & 0x0f) << 8) | message[16];
  const trackType = misc & 0x03;
  const positioned = message.subarray(5, 11).some(byte => byte !== 0);
  return {
    alertStatus: message[1] >> 4,
    addressType: ADDRESS_TYPES[message[1] & 0x0f] ?? message[1] & 0x0f,
    address: ((message[2] << 16) | (message[3] << 8) | message[4]).toString(16).toUpperCase().padStart(6, '0'),
    latitude: positioned ? degrees(message, 5) : null,
    longitude: positioned ? degrees(message, 8) : null,
    pressureAltitudeFt: altitude === 0xfff ? null : altitude * 25 - 1000,
    airborne: Boolean(misc & 0x08),
    extrapolated: Boolean(misc & 0x04),
    trackType: TRACK_TYPES[trackType],
    nic: message[13] >> 4,
    nacp: message[13] & 0x0f,
    groundSpeedKt: horizontal === 0xfff ? null : horizontal,
    climbFpm: vertical === 0x800 ? null : signed(vertical, 12) * 64,
    trackDeg: trackType === 0 ? null : (message[17] * 360) / 256,
    emitterCategory: EMITTER_CATEGORIES[message[18]] ?? message[18],
    callsign: String.fromCharCode(...message.subarray(19, 27)).trim() || null,
    emergencyCode: message[27] >> 4,
  };
}

function degrees(message, at) {
  return (signed((message[at] << 16) | (message[at + 1] << 8) | message[at + 2], 24) * 180) / 2 ** 23;
}

function signed(value, bits) {
  return value >= 2 ** (bits - 1) ? value - 2 ** bits : value;
}
