# hardware/parts/l76k

The Quectel L76K on the LilyGO T-Echo, an AT6558 receiver that takes the CASIC `$PCAS` sentences and acknowledges none of them. `l76k.h` is the driver, and `model.h` with `model_sentences.h` is the host model the simulator flies. What the parser, the validity rule, the port rate and the settle do with its output is in `../../../core/gnss/README.md`.

## Four solutions a second

The Go runs the L76K at 4 Hz, so its solutions fall at 0, 250, 500 and 750 ms of each UTC second. Those are the quarter seconds ADS-L 4 SRD-860 Issue 2 G.1.1 counts the Timestamp in, so every solution can be named to within its 10 ms. A Traffic burst carries the newest solution as measured, and G.1.16 keeps it off the air once that solution is 500 ms old (`../../../core/timing/README.md`, The age of a position). At 1 Hz most airborne bursts would be held.

Two documents set the command. The CASIC Multimode Satellite Navigation Receiver Protocol Specification V4.2.0.3, §1.6.3, lists `$PCAS02` intervals of 1000, 500, 250, 200 and 100 ms, with 250 as 4 Hz. Quectel's L76K GNSS Protocol Specification V1.1, §2.3.2, lists 1000, 500 and 200, and adds: "It is required to set the type of NMEA sentences output to single and change the baud rate to 115200 bps when the <Interval> is less than 1000." 5 Hz is the faster rate Quectel prints, and it would put four solutions in five off G.1.1's quarters.

The driver brings the part up in three steps:

| Step | Sentences | What says it worked |
|---|---|---|
| 1, at the boot rate | `$PCAS04,7`, `$PCAS03,1,0,1,0,1,0,0,0,0,0,,,0,0`, `$PCAS11,6`, `$PCAS02,1000` | no GLL or VTG for `kVerifyWindowMs` |
| 2 | `$PCAS01,5`, 115200 | one sentence that checksums at the new rate |
| 3, at 115200 | `$PCAS02,250`, `$PCAS03,,,4,,,,,,,,,,,` | `kMinVerifySolutions` solutions in `kQuarterVerifyWindowMs` |

Step 3 comes only after step 2 has proved the part answers at 115200, as V1.1 requires. A port that cannot retune, or a part that does not take `$PCAS01`, stays at 9600 and 1 Hz. A part that takes step 3 and keeps solving once a second fails the count, and after `kMaxConfigAttempts` the driver reports it `DEGRADED`. It still delivers a fix once a second, and `L76k::solving_quarters()` is false.

## The sentences

| What the Go reads | Sentence |
|---|---|
| latitude and longitude | RMC |
| height above sea level and the geoid separation | GGA |
| ground speed and track | RMC |
| UTC to the millisecond | RMC, and GGA's stamp for the check below |
| date | RMC |
| fix validity | RMC's status and GGA's quality |
| HDOP and satellites used | GGA |
| VDOP and the fix mode | GSA |
| vertical rate | none, differenced from GGA's height or taken from the barometer |

GGA and RMC go out every solution, GSA every fourth, once a second as at 1 Hz, and GSV only while the satellites page is up, one set in nine solutions. The two every solution are 150 bytes, 13 ms of 115200. They depart from V1.1's single sentence type, because no sentence the part sends carries a height and a velocity together: GGA has no speed, track or date, RMC no height, and CASIC's DHV no position. A Traffic burst takes both from one solution, so the driver publishes a solution only when its GGA and its RMC name the same instant (`gnss::NmeaParser::same_epoch()`).

## What the bench checks

None of this has been measured on a unit. Each item below is a capture with the diagnostics armed.

- `$PCAS02,250` at 115200 gives four RMC a second, stamped `.000`, `.250`, `.500` and `.750`, on the firmware `$PCAS06` names. The G.1.1 case `ADS-L.4.SRD860.G.1.1: the L76K solves on the quarter its stamp names, within 10 ms` is skipped as `unmeasured:` until then.
- The `.000` solution is the one the PPS edge opens, and `solution_phase_ms` stays under 250 ms on every quarter. Past 250 ms a burst can find its solution too old.
- GGA and RMC every solution, against V1.1's single-sentence note: `gnss_solutions` counts 14400 in an hour with no solution dropped and no overrun.
- `kBurstStartMs`, the 70 ms SoftRF measured at 1 Hz, still holds at 4 Hz.
- The current at 4 Hz, against the 29 mA tracking figure of the L76K hardware design V1.0 table 2 that `scripts/power_budget.py` books.
- 4 Hz survives `$PCAS10` hot, warm and cold starts, as the model assumes.
