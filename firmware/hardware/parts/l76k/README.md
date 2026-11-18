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
| 3, at 115200 | `$PCAS02,250`, `$PCAS03,,,4,,,,,,,,,,,` | `kMinQuarterSteps` quarter steps in `kQuarterVerifyWindowMs` |

Step 3 comes only after step 2 has proved the part answers at 115200, as V1.1 requires. A port that cannot retune, or a part that does not take `$PCAS01`, stays at 9600 and 1 Hz. A quarter step is two consecutive pairs whose stamps are 250 ms apart, each on a quarter second. A part at 4 Hz makes eight in the window, and a part at any other rate makes none, on the quarters or off them: 1 Hz steps by 1000 ms and 5 Hz stamps `.200` and `.400`. A pair that names no time, `$GNGGA,,,,,,0,00,25.5,,,,,,*64` and `$GNRMC,,V,,,,,,,,,,N,V*37` from a part that has lost UTC (LilyGO-Modem-Series issue 95), is no solution to judge, and never counts as off cadence. A part that fails step 3 `kMaxConfigAttempts` times is asked for 2 Hz, `$PCAS02,500` and GSA every second solution, judged the same way on half steps. A part that refuses that too gets step 1 again, `$PCAS02,1000` and the whole 1 Hz sentence set: it delivers a fix once a second, `L76k::solving_quarters()` is false, and `air.tx_held_for_fix` counts the positions G.1.16 then holds. A refused 4 Hz is asked again every `kQuarterRetryMs`, five minutes, and at once after a warm or cold start. While ready the driver keeps counting steps in each window, and a part heard drifting off its rate gets that step again.

The driver empties the port before every configuration, and publishes no pair completed less than `kPairSpacingFloorMs` after the one before: those are a backlog draining, not the part's cadence (`../../../core/gnss/README.md`, What the device can and cannot see about a late solution).

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

- 250 ms is documented in the CASIC protocol, §1.6.3, and not in Quectel's L76K interval table, which lists 1000, 500 and 200 ms. Whether this firmware takes it is the first thing to see.
- `$PCAS02,250` at 115200 gives four RMC a second, stamped `.000`, `.250`, `.500` and `.750`, on the firmware `$PCAS06` names. The G.1.1 case `ADS-L.4.SRD860.G.1.1: the L76K solves on the quarter its stamp names, within 10 ms` is skipped as `unmeasured:` until then.
- The `.000` solution is the one the PPS edge opens, and `solution_phase_ms` stays under 250 ms on every quarter. Past 250 ms a burst can find its solution too old.
- GGA and RMC every solution, against V1.1's note that a rate under 1000 ms needs "the type of NMEA sentences output" set "to single": `gnss_solutions` counts 14400 in an hour with no solution dropped and no overrun.
- If the part does not keep up with two sentences, CASIC's binary NAV-PV (0x01 0x03, CASIC §2.7.4) carries position, ellipsoid height, velocity and their accuracies in one 80-byte message. It dates itself by time since power-on rather than UTC, so it would need NAV-TIMEUTC beside it, and the driver would need a binary reader. It is the alternative to try on the bench, not something the driver sends.
- `kBurstStartMs`, the 70 ms SoftRF measured at 1 Hz, still holds at 4 Hz.
- The assumption the device cannot check for itself: the part delivers each solution well under 250 ms after the epoch it names, from power-up on. Measure it against an independent time source: put the PPS pin and the UART TX line on one logic analyser, decode the RMC stamps, and read the time from each edge to the end of the RMC that names it, from a cold power-up and over an hour, at 4 Hz and with the satellites page open. Every value must stay under 250 ms. The skipped case `ADS-L.4.SRD860.G.1.16: an L76K that delivers every solution a second late is refused` waits on it.
- A part that loses UTC sends the no-time pair above, and solves on the quarters again when the time comes back.
- Whether this firmware takes `$PCAS02,500`, the 2 Hz the driver falls back to.
- The current at 4 Hz, against the 29 mA tracking figure of the L76K hardware design V1.0 table 2 that `scripts/power_budget.py` books.
- 4 Hz survives `$PCAS10` hot, warm and cold starts, as the model assumes.
