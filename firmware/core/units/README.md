# core/units

The one place a measurement changes unit, and the reason every conversion in it rounds.

A device measures in the units of its instruments and transmits in the units of a protocol, and the two are never the same. The receiver resolves hundredths of a degree of track, ADS-L G.1.10 carries nine bits for the whole circle. The barometer resolves millimetres of altitude, G.1.7 carries metres. Own-ship state therefore holds what the instruments resolve, and a conversion to a coarser unit happens once, at the edge that needs it: the protocol encoder, the screen, the NMEA output, the flight log.

Truncation is the trap this file exists to close. `(mm * 19685) / 100000` on a 1.524 m/s climb gives 299 fpm for a rate the pilot set to 300, and nothing downstream can tell that the missing foot was arithmetic rather than air. Worse, a truncated value fed back into a control path biases it: a turn rate truncated to whole degrees read 2 deg/s in a standard rate turn, which is a third low. Every conversion here uses `divide_rounded` from `core/util/intmath.h`, and so should any conversion elsewhere.

Going up in precision is exact and needs no rounding: `to_mm_s(QuarterMetresPerSecond)` multiplies by 250. That is the direction a received frame travels, and it is why the geometry the alarm computes can be done in millimetres whatever the wire carried. Going down rounds and clamps, and that is the direction a transmitted frame travels.

The wire units are named for what they are, not for their size: `QuarterMetresPerSecond` is ADS-L G.1.8's ground speed, `EighthMetresPerSecond` is G.1.9's climb rate, `Cordic9` is the 512-unit circle of G.1.10. A field that carries one of those is holding a protocol value, and holding a protocol value in the middle of a calculation is the bug.
