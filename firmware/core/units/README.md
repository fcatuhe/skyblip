# core/units

The one place a measurement changes unit. Every conversion here rounds.

Own-ship state holds what the instruments resolve, such as hundredths of a degree of track and millimetres of altitude. The protocol carries coarser units: nine bits for the circle (ADS-L G.1.10), whole metres of altitude (G.1.7). Convert to a coarser unit once, at the edge that needs it: the protocol encoder, the screen, the NMEA output, the flight log.

Round, never truncate. Truncated, `(mm * 19685) / 100000` turns a 1.524 m/s climb into 299 fpm, and a turn rate truncated to whole degrees reads a third low in a standard rate turn. Use `divide_rounded` (`protocols/common/geometry.h`) for any conversion, here or elsewhere.

Going up in precision is exact: `to_mm_s(QuarterMetresPerSecond)` multiplies by 250. A received frame goes that way, so the alarm's geometry works in millimetres whatever the wire carried. Going down rounds and clamps, which is the way a transmitted frame goes.

The wire units are named for what they are: `QuarterMetresPerSecond` is ADS-L G.1.8's ground speed, `EighthMetresPerSecond` is G.1.9's climb rate, `Cordic9` is G.1.10's 512-unit circle. Never hold one of them in the middle of a calculation.
