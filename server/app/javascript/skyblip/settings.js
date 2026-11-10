export const SETTING_KEYS = ['aircraft_type', 'alarm_enabled', 'alarm_volume', 'units', 'callsign'];

const NUMBERS = new Set(['aircraft_type']);

export function settingsOf(reply, keys = SETTING_KEYS) {
  return Object.fromEntries(keys.filter(key => key in reply).map(key => [key, reply[key]]));
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
  };
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
