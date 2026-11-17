export const SETTING_KEYS = ['aircraft_type', 'units', 'callsign', 'aircraft_address', 'alarm'];

const NUMBERS = new Set(['aircraft_type']);
const AIRCRAFT_ADDRESS = /^[0-9A-F]{6}$/;

// INFO: fc 17nov26 a paired Bravo's alarm is its Go's, so the form shows it and never sends it
export function settingsOf(reply, keys = SETTING_KEYS) {
  const held = keys.filter(key => key in reply && !(key === 'alarm' && reply.alarm_from_go === true));
  return Object.fromEntries(held.map(key => [key, key === 'aircraft_address' ? aircraftAddressText(reply[key]) : reply[key]]));
}

export function soundsOf(reply) {
  return reply.annunciator_fitted ?? null;
}

export function goAlarmOf(reply) {
  return reply.alarm_from_go === true ? reply.alarm : null;
}

export function settingsFrame(settings) {
  if (!('aircraft_address' in settings)) return settings;
  const text = settings.aircraft_address;
  return { ...settings, aircraft_address: text === '' ? 0 : AIRCRAFT_ADDRESS.test(text) ? Number.parseInt(text, 16) : text };
}

function aircraftAddressText(address) {
  return address === 0 ? '' : address.toString(16).toUpperCase().padStart(6, '0');
}

export function changedSettings(stored, values, keys = SETTING_KEYS) {
  return Object.fromEntries(Object.entries(values).filter(([key, value]) => keys.includes(key) && stored?.[key] !== value));
}

export function statusOf(reply) {
  return {
    flight: reply.flight ?? null,
    upload: reply.upload === true,
    batteryPercent: reply.battery_percent ?? null,
    charging: reply.charging === true,
    powerLevel: reply.power_level ?? null,
    wentDarkFlat: reply.went_dark_flat === true,
    bravo: reply.bravo ?? null,
    ownTransponder: ownTransponderOf(reply),
  };
}

function ownTransponderOf({ own_transponder: address, own_transponder_callsign: callsign, own_transponder_category: type }) {
  if (address === undefined) return null;
  return { aircraft_address: aircraftAddressText(address), callsign: callsign ?? '', aircraft_type: type ?? 0 };
}

// INFO: fc 15nov26 as go::adopt writes it, so another aircraft's callsign never stays beside this one's address
export function heardSettings(heard, settings) {
  const next = { aircraft_address: heard.aircraft_address };
  if ('callsign' in settings && heard.callsign) next.callsign = heard.callsign;
  else if ('callsign' in settings && heard.aircraft_address !== settings.aircraft_address) next.callsign = '';
  if ('aircraft_type' in settings && heard.aircraft_type !== 0) next.aircraft_type = heard.aircraft_type;
  return next;
}

export function readSetting(field) {
  if (field.type === 'checkbox') return field.checked;
  if (NUMBERS.has(field.name)) return Number(field.value);
  if (field.tagName === 'SELECT') return field.value;
  return field.value.toUpperCase().trimEnd();
}

export function showSetting(field, value) {
  if (field.type === 'checkbox') {
    field.checked = value;
    return;
  }
  if (field.tagName === 'SELECT' && ![...field.options].some(option => option.value === String(value))) {
    field.add(new Option(String(value), value));
  }
  field.value = value;
}
