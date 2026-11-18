const HOUR_S = 3600

// INFO: fc 15nov26 a capture keeps minutes or days, so the span is read in the unit that fits it
export function span(seconds) {
  return seconds >= 2 * HOUR_S ? `${Math.round(seconds / HOUR_S)} h` : `${Math.round(seconds / 60)} min`
}
