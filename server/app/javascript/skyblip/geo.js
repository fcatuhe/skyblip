const EARTH_RADIUS_M = 6371008.8;
const radians = degrees => (degrees * Math.PI) / 180;
const degrees = radians => (radians * 180) / Math.PI;

export const FEET_PER_METRE = 3.28084;
export const KNOTS_PER_METRE_SECOND = 1.943844;
export const FPM_PER_METRE_SECOND = 196.8504;

export function hasPosition(place) {
  return Number.isFinite(place?.latitude) && Number.isFinite(place?.longitude);
}

export function distanceM(from, to) {
  const dLatitude = radians(to.latitude - from.latitude);
  const dLongitude = radians(to.longitude - from.longitude);
  const h = Math.sin(dLatitude / 2) ** 2 +
    Math.cos(radians(from.latitude)) * Math.cos(radians(to.latitude)) * Math.sin(dLongitude / 2) ** 2;
  return 2 * EARTH_RADIUS_M * Math.asin(Math.min(1, Math.sqrt(h)));
}

export function bearingDeg(from, to) {
  const dLongitude = radians(to.longitude - from.longitude);
  const y = Math.sin(dLongitude) * Math.cos(radians(to.latitude));
  const x = Math.cos(radians(from.latitude)) * Math.sin(radians(to.latitude)) -
    Math.sin(radians(from.latitude)) * Math.cos(radians(to.latitude)) * Math.cos(dLongitude);
  return (degrees(Math.atan2(y, x)) + 360) % 360;
}

export function offset(from, northM, eastM) {
  return {
    latitude: from.latitude + degrees(northM / EARTH_RADIUS_M),
    longitude: from.longitude + degrees(eastM / (EARTH_RADIUS_M * Math.cos(radians(from.latitude)))),
  };
}
