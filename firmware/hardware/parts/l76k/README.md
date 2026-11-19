# hardware/parts/l76k

The Quectel L76K on the LilyGO T-Echo, an AT6558 receiver that takes the CASIC `$PCAS` sentences and acknowledges none of them. `l76k.h` is the driver: `l76k.cpp` brings the part up, `l76k_bring_up.cpp` names its commands, journals them and recovers a silent part, and `l76k_solutions.cpp` publishes what it solves. `model.h` with `model_commands.h` and `model_sentences.h` is the host model the simulator flies. What the parser, the validity rule, the port rate and the settle do with its output is in `../../../core/gnss/README.md`.

## Four solutions a second

The product chooses the rate and the driver applies it: `L76k` takes a `ports::SolutionRate` at construction, from the board, from the product's `kSolutionRate`. Asked for `Second`, the driver stops after step 2 below and never sends a `$PCAS02` under 1000 ms. The raise to 115200 still happens, because it predates 4 Hz: it is what lets a GSV set fit a solution (`../../../core/gnss/README.md`). Asked for `Quarters`, it runs step 3. The Go asks `Quarters` (`../../../products/skyblip_go/README.md`, The GNSS rate).

The Go runs the L76K at 4 Hz, so its solutions fall at 0, 250, 500 and 750 ms of each UTC second. Those are the quarter seconds ADS-L 4 SRD-860 Issue 2 G.1.1 counts the Timestamp in, so every solution can be named to within its 10 ms. A Traffic burst carries the newest solution as measured, and G.1.16 keeps it off the air once that solution is 500 ms old (`../../../core/timing/README.md`, The age of a position). At 1 Hz most airborne bursts would be held.

Two documents set the command. The CASIC Multimode Satellite Navigation Receiver Protocol Specification V4.2.0.3, §1.6.3, lists `$PCAS02` intervals of 1000, 500, 250, 200 and 100 ms, with 250 as 4 Hz. Quectel's L76K GNSS Protocol Specification V1.1, §2.3.2, lists 1000, 500 and 200, and adds: "It is required to set the type of NMEA sentences output to single and change the baud rate to 115200 bps when the <Interval> is less than 1000." 5 Hz is the faster rate Quectel prints, and it would put four solutions in five off G.1.1's quarters.

The driver brings the part up in three steps:

| Step | Sentences | What says it worked |
|---|---|---|
| 1, at the boot rate | `$PCAS04,7`, `$PCAS03,1,0,1,0,1,0,0,0,0,0,,,0,0`, `$PCAS11,6`, `$PCAS02,1000` | no GLL or VTG for `kVerifyWindowMs` |
| 2 | `$PCAS01,5`, 115200 | one sentence that checksums at the new rate |
| 3, at 115200 | `$PCAS02,250`, `$PCAS03,,,4,,,,,,,,,,,` | `kMinQuarterSteps` quarter steps in `kQuarterVerifyWindowMs` |

Step 3 comes only after step 2 has proved the part answers at 115200, as V1.1 requires. A port that cannot retune, or a part that does not take `$PCAS01`, stays at 9600 and 1 Hz. A quarter step is two consecutive pairs whose stamps are 250 ms apart, each on a quarter second. A part at 4 Hz makes eight in the window, and a part at any other rate makes none, on the quarters or off them: 1 Hz steps by 1000 ms and 5 Hz stamps `.200` and `.400`. A pair that names no time, `$GNGGA,,,,,,0,00,25.5,,,,,,*64` and `$GNRMC,,V,,,,,,,,,,N,V*37` from a part that has lost UTC (LilyGO-Modem-Series issue 95), is no solution to judge, and never counts as off cadence. Step 3 is sent once a session. A part that fails it is asked for 2 Hz, `$PCAS02,500` and GSA every second solution, judged the same way on half steps, up to `kMaxConfigAttempts` times. A part that refuses that too gets step 1 again, `$PCAS02,1000` and the whole 1 Hz sentence set: it delivers a fix once a second, `L76k::solving_quarters()` is false, and `air.tx_held_for_fix` counts the positions G.1.16 then holds. The session ends at a warm, cold or factory start, which asks 4 Hz once more after the part's `kRestartSettleMs` reboot. While ready the driver keeps counting steps in each window. A part heard drifting off 4 Hz is put at 2 Hz, and one drifting off 2 Hz is asked for 2 Hz again. Go 000001 went silent 19 minutes after boot on a build that asked 4 Hz again every five minutes, so the driver now sends `$PCAS02,250` once and the capture says what came of it.

The driver empties the port before every configuration. Each drain publishes its newest GGA and RMC pair, and only if its stamp is later than the last pair published, so a replayed pair never goes out and a pass late for two pairs still publishes the newer. A pair drained behind another is marked `backlogged` and anchors nothing. A stamp more than `kSentenceMaxAgeMs` behind the last is the part's clock stepping back, not a replay, and is published (`../../../core/gnss/README.md`, What the device can and cannot see about a late solution).

## A silent part

A part that has said nothing for `kSilenceMs`, 3 s, while ready or degraded is probed: the driver sends the wake byte at each rate of `kBaudCandidates` in turn and listens `kProbeListenMs`, a wake and one 1 Hz solution, at each. A round takes 9 s and one starts every `kProbePeriodMs`, 10 s, for as long as the part stays silent. The first sentence that checksums ends the probing, and the part is configured again from step 1 at that rate. A verification that heard nothing after the part had been heard once probes too, so a part silent after `$PCAS02,250` is never walked down the rates to an end. Degraded now means only a part that talks and does not obey.

After `kSilentRoundsBeforeReset`, three rounds, the driver holds the part's RESET_N low for `kResetHoldMs`, 150 ms, then waits `kRestartSettleMs` and probes again from 9600. Quectel's L76K Hardware Design V1.0, §4.2.1, asks for at least 100 ms low and for RESET_N to be wired "so that it can be used to reset the module if the module enters an abnormal state". The T-Echo Plus wires it to P1.05, active low: the devicetree's `gnss_reset_gpio`, LilyGO's T-Echo `utilities.h` (`Gps_Reset_Pin _PINNUM(1,5)`, beside `Gps_Wakeup_Pin _PINNUM(1,2)`), and Meshtastic's `variants/nrf52840/t-echo-plus/variant.h` (`PIN_GPS_REINIT (32 + 5)`). The part has no power switch of its own: P0.12 feeds the e-paper, the external flash and the sensor bus with it, so the reset line is the one way to restart the part alone. A board that hands the driver no reset line probes for ever and never resets.

## What a capture records

A Full capture writes one `receiver` record for each thing the driver does to the part: each command it sends, the wake byte, every `$PCAS` sentence and the reset pulse, each change of configuration stage, and each verification verdict. Each record carries the uptime, the port's rate, the interval asked, the interval between the last two distinct stamps, the last stamp's milliseconds, and the steps and sentences counted in the open window (`../../../core/diagnostics/README.md`). The board drains the driver's journal of `kJournalEvents` each pass onto `bus::Bus::gnss_events`, and `OwnshipService` records it. The diagnostics dump's gnss group carries the same story in one frame: `interval_ms`, `interval_seen_ms`, `verdict` and `verify_steps` of the last window, `quarter_verdict` and `quarter_steps` of the one 4 Hz attempt, `commands`, `last_command` and `last_command_at_ms`, `receiver_probes` and `receiver_resets`, beside `config`, the stage.

Two questions the device test left open read off a unit this way:

- Did the part refuse 250 ms? Read `quarter_verdict` and `quarter_steps` in the dump. `passed` with 3 or more steps is a part at 4 Hz. `off_cadence` with 0 steps is a refusal, and the `receiver` record with `event` `verdict` and `interval_ms` 250 says what the part did instead: `interval_seen_ms` 1000 and `stamp_ms` 0 is a part that stayed at 1 Hz, 500 one that took 2 Hz. `silent` is a part that stopped talking after `$PCAS02,250`, and the records after it show the probing and the reset.
- What preceded a silence? The `receiver` records before the one whose `config_stage` turns `probing`: the last `command` sent and its `uptime_ms`, against the gnss and pps records that stopped. With no command in the minutes before, the part went silent on its own.

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
- The assumption the device cannot check for itself: the part delivers each solution well under 250 ms after the epoch it names, from power-up on. The part's own PPS and UART cannot settle it, since a receiver that delays everything by a second keeps its PPS and its stamps consistent with each other, and a logic analyser alone gives durations, not UTC. So the measurement needs a reference the part does not drive: a second GNSS receiver's PPS with its own time output, or a GNSS-disciplined time reference. Put that reference's PPS and time, the L76K's PPS and its UART TX on one capture. For each L76K RMC, find the reference second its stamp names and read the time from that reference edge plus the stamp's milliseconds to the end of the RMC. Do it from a cold power-up and over an hour, at 4 Hz and with the satellites page open. Every value must stay under 250 ms. The skipped case `ADS-L.4.SRD860.G.1.16: an L76K that delivers every solution a second late is refused` waits on it, and the device's G.1.16 conformity is conditional on this measurement.
- On the same capture, at 4 Hz with the satellites page open so GSV rides some bursts: the shortest time between the ends of two consecutive pairs, and the spread of each pair's delivery after its epoch. The driver no longer assumes either, but the figures say how far a GSV set moves a solution inside its quarter.
- A part that loses UTC sends the no-time pair above, and solves on the quarters again when the time comes back.
- Whether this firmware takes `$PCAS02,500`, the 2 Hz the driver falls back to.
- The current at 4 Hz, against the 29 mA tracking figure of the L76K hardware design V1.0 table 2 that `scripts/power_budget.py` books.
- 4 Hz survives `$PCAS10` hot, warm and cold starts, as the model assumes.
- A part held in RESET_N for 150 ms comes back at 9600 on its factory settings, and answers the probe within the 1.5 s it listens at that rate.
