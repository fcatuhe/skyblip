# hardware/parts/l76k

The Quectel L76K on the LilyGO T-Echo, an AT6558 receiver that takes the CASIC `$PCAS` sentences and acknowledges none of them. `l76k.h` is the driver: `l76k.cpp` brings the part up, `l76k_recovery.cpp` finds a silent part again, and `l76k_solutions.cpp` publishes what it solves. `model.h` with `model_commands.h` and `model_sentences.h` is the host model the simulator flies. What the parser, the validity rule, the port rate and the settle do with its output is in `../../../core/gnss/README.md`.

## Two solutions a second

The product chooses the rate: `L76k` takes a `ports::SolutionRate` at construction, from the product's `kSolutionRate` through the board. Asked for `Second`, the driver stops after step 2 below and never sends a `$PCAS02` under 1000 ms. It still raises the port to 115200, so a GSV set fits a solution (`../../../core/gnss/README.md`). Asked for `Half`, it runs step 3. The Go asks `Half` (`../../../products/skyblip_go/README.md`, The GNSS rate).

At 2 Hz the solutions fall at 0 and 500 ms of each UTC second, two of the quarter seconds ADS-L 4 SRD-860 Issue 2 G.1.1 counts the Timestamp in, so every solution is named to within its 10 ms. G.1.16 keeps a Traffic burst off the air once its solution is 500 ms old, so at 1 Hz most airborne bursts would be held. A position drawn between 490 ms and the `.500` solution's arrival waits for it and is drawn again (`../../../core/timing/README.md`, The age of a position).

500 ms is in both command documents. The CASIC Multimode Satellite Navigation Receiver Protocol Specification V4.2.0.3, §1.6.3, lists `$PCAS02` intervals of 1000, 500, 250, 200 and 100 ms. Quectel's L76K GNSS Protocol Specification V1.1, §2.3.2, lists 1000, 500 and 200, and requires single sentence output and 115200 bps below 1000. Go 000001, on firmware `URANUS5,V5.3.0.0`, ignored `$PCAS02,250` and kept 500 ms. 5 Hz would put four solutions in five off G.1.1's quarters.

The driver brings the part up in three steps:

| Step | Sentences | What says it worked |
|---|---|---|
| 1, at the boot rate | `$PCAS04,7`, `$PCAS03,1,0,1,0,1,0,0,0,0,0,,,0,0`, `$PCAS11,6`, `$PCAS02,1000` | no GLL or VTG for `kVerifyWindowMs` |
| 2 | `$PCAS01,5`, 115200 | one sentence that checksums at the new rate |
| 3, at 115200 | `$PCAS02,500`, `$PCAS03,,,2,,,,,,,,,,,` | `kMinHalfSteps` half steps in `kRateVerifyWindowMs` |

- Step 3 comes only after step 2 has proved the part answers at 115200, as V1.1 requires.
- A product that asked 1 Hz settles at 9600 when the port cannot retune or the part refuses `$PCAS01`. A product that asked 2 Hz never settles below it: the driver recovers the part instead.
- A half step is two consecutive GGA and RMC pairs whose stamps are 500 ms apart, each on `.000` or `.500`. A part at 2 Hz makes four in the window, and a part at any other rate makes none.
- A pair that names no time, from a part that has lost UTC (LilyGO-Modem-Series issue 95), is no solution to judge and never counts as off cadence.
- A part that fails step 3 `kMaxConfigAttempts` times is recovered, and one that falls silent after it is probed. While ready, the driver keeps counting half steps, and asks again for 2 Hz when a part drifts off it.

## Recovery

The driver never settles for less than the product asked. A part that refuses 2 Hz three times, sits at 9600 when 2 Hz is asked, or talks and disobeys the `$PCAS` set three times is recovered:

- The driver enters `DEGRADED` and counts the round in `L76k::recoveries()`, the dump's `receiver_recoveries`.
- After a backoff of `kRecoveryBackoffMs`, 10 s, then 30 s, then every 60 s, it holds RESET_N low and configures the part again from step 1. A board with no reset line starts again from the wake byte.
- It never gives up, so a part that keeps refusing shows as `DEGRADED` and a climbing count, never as a unit quietly at 1 Hz. The backoff restarts from 10 s once the part holds the asked rate.

`L76k::holds_rate()` is true only while the part is ready at the asked rate.

The driver empties the port before every configuration. Each drain publishes its newest GGA and RMC pair, and only when its stamp is later than the last pair published, so a replayed pair never goes out. A pair drained behind another is marked `backlogged` and anchors nothing. A stamp more than `kSentenceMaxAgeMs` behind the last is the part's clock stepping back, not a replay, and is published (`../../../core/gnss/README.md`, What the device can and cannot see about a late solution).

## A silent part

A part that has said nothing for `kSilenceMs`, 3 s, while ready or degraded is probed. The driver sends the wake byte at each rate of `kBaudCandidates` in turn and listens `kProbeListenMs`, a wake and one 1 Hz solution, at each. A round takes 9 s, and one starts every `kProbePeriodMs`, 10 s, while the part stays silent. The first sentence that checksums ends the probing, and the part is configured again from step 1 at that rate. A verification that heard nothing from a part heard before also probes, so a part silent after `$PCAS02,500` is never walked down the rates.

After `kSilentRoundsBeforeReset`, three rounds, the driver holds RESET_N low for `kResetHoldMs`, 150 ms, waits `kRestartSettleMs` and probes again from 9600. Quectel's L76K Hardware Design V1.0, §4.2.1, asks for at least 100 ms low, and for RESET_N to be wired to reset a module in an abnormal state. The T-Echo Plus wires it to P1.05, active low: the devicetree's `gnss_reset_gpio`, LilyGO's T-Echo `utilities.h` (`Gps_Reset_Pin _PINNUM(1,5)`) and Meshtastic's `variants/nrf52840/t-echo-plus/variant.h` (`PIN_GPS_REINIT (32 + 5)`). P0.12 powers the part together with the e-paper, the external flash and the sensor bus, so the reset line is the one way to restart the part alone. A board that hands the driver no reset line probes forever and never resets.

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

GGA and RMC go out every solution, GSA every second one, so once a second, and GSV only while the satellites page is up, one set in nine solutions. GGA and RMC are 150 bytes, 13 ms at 115200. They depart from V1.1's single sentence type, because no sentence carries a height and a velocity together: GGA has no speed, track or date, RMC no height, and CASIC's DHV no position. A Traffic burst takes both from one solution, so the driver publishes a solution only when its GGA and its RMC name the same instant (`gnss::NmeaParser::same_epoch()`).

## What the bench checks

None of this has been measured on a unit beyond Go 000001. Each item is a capture with the diagnostics armed.

- `$PCAS02,500` at 115200 gives two RMC a second, stamped `.000` and `.500`, on the firmware `$PCAS06` names. The case `ADS-L.4.SRD860.G.1.1: the L76K solves on the quarter its stamp names, within 10 ms` stays skipped as `unmeasured:` until a capture times the stamps against PPS.
- `solution_phase_ms` stays under 250 ms on both solutions. Go 000001 read a `.000` median of 57 ms and a worst of 93 ms, 160 ms with the satellites page up.
- GGA and RMC every solution, against V1.1's single-sentence rule: `gnss_solutions` counts 7200 in an hour, with no solution dropped and no overrun. If the part cannot keep up, try CASIC's binary NAV-PV (0x01 0x03, CASIC §2.7.4), with NAV-TIMEUTC beside it, since NAV-PV dates itself from power-on. The driver would need a binary reader.
- `kBurstStartMs`, the 70 ms SoftRF measured at 1 Hz, still holds at 2 Hz.
- The part delivers each solution well under 250 ms after the epoch it names, from power-up on. The device cannot check this itself: a receiver late by a whole second keeps its PPS and stamps consistent. Capture a second receiver's PPS and time output, or a GNSS-disciplined reference, with the L76K's PPS and UART TX. For each RMC, measure from the reference edge its stamp names, plus the stamp's milliseconds, to the end of the RMC, from a cold power-up and over an hour, at 2 Hz with the satellites page open. Every value must stay under 250 ms. The skipped case `ADS-L.4.SRD860.G.1.16: an L76K that delivers every solution a second late is refused` waits on it, and the device's G.1.16 conformity depends on it.
- On the same capture: the shortest time between the ends of two consecutive pairs, and the spread of each pair's delivery after its epoch, to see how far a GSV set moves a solution inside its quarter.
- A part that loses UTC sends the no-time pair, and solves on `.000` and `.500` again when the time comes back.
- The current at 2 Hz, against the 29 mA tracking figure of the L76K Hardware Design V1.0 table 2 that `scripts/power_budget.py` books.
- 2 Hz survives `$PCAS10` hot, warm and cold starts, as the model assumes.
- A part held in RESET_N for 150 ms comes back at 9600 on its factory settings, and answers the probe within the 1.5 s it listens at that rate.
