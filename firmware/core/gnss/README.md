# core/gnss

A receiver produces solutions on its own cadence, two a second on the Go, whether or not it sees the sky. A fix is a solution that passed `validity.h`: `nmea.h` parses a `GnssSolution`, `FixValidity` judges it, and only what passes is called a fix downstream.

`GnssSolution::fix_valid` is filled twice with one meaning. The parser sets it from RMC status `A`, the receiver's own claim. `parts::L76k::poll` and `parts::Ag3335::poll` overwrite it with `FixValidity`'s verdict before publishing, so the receiver's claim never reaches the bus. The verdict needs GGA and RMC both heard within `kSentenceMaxAgeMs`, as SoftRF does, a real date, and no jump between consecutive solutions, as moshe-braner's gate does.

Both receivers share the parser. Bravo's AG3335 sends NMEA 4.10 on the `GN` talker (`../../hardware/parts/ag3335/README.md`). Its GSA names Galileo as system 3, which `system_of_gsa_id` does not know, so a Galileo satellite in use matches any constellation with the same id. Only the satellites page reads that, and Bravo has none. Its GGA without a fix is 37 bytes, under `kGgaLengthFloorBytes`, so its no-fix reason reads `NO GGA`.

## How far a receiver with no fix has got

`acquisition.h` splits `NO FIX` into three rungs, read from the sentences already on the wire, so it costs no bytes. Each rung tells the pilot what to do:

| Rung | Means | Pilot |
|---|---|---|
| `GPS OFF` | nothing parsed for `kSentenceMaxAgeMs` | takes the device to a bench |
| `MOVE OUT` | solutions arrive with no usable date | moves the device |
| `FINDS FIX` | a date is decoded | waits another minute |

A receiver can report a date only after it has read a satellite's navigation message, so `FINDS FIX` means it sees sky. The parser refuses the MTK 1980 date, so a fake date cannot promote the ladder. `Stage::Fixed` has no word, because the glass then reads `GROUND`, `TAXI` or `FLIGHT`.

The words are ASD-STE100, like the rest of the glass (`../../products/skyblip_go/pages/README.md`, The frame every asking page is set in), and each fits the nine double-height characters `nearby` leaves a unit's row.

The stage carries the instant it was entered. The elapsed seconds show on `status`, which redraws every second anyway, and never on the radar ring, where they would refresh the panel every second.

## The port rate, and the deadline it is chosen against

The L76K boots at `L76k::kFactoryBaudRate`, 9600 (L76K GNSS Protocol Specification V1.1, Appendix C Table 17), and is worked at `kTargetBaudRate`, 115200, the top of `$PCAS01` (V1.1 §2.3.1).

The deadline is a quarter second. The part solves every 500 ms, on `.000` and `.500`, and `Transmitter::kFixMaxAgeMs` holds a burst whose solution would be over 500 ms old on air. So a solution must be parsed within 250 ms of the quarter it names, because the clock's anchor trusts no later one (`../timing/README.md`, The age of a position). The part starts talking 70 ms after the second, RMC at 135, as SoftRF measured at 1 Hz (`.../src/driver/GNSS.cpp:83-91`), which is `kBurstStartMs`. The bench checks it at 2 Hz (`../../hardware/parts/l76k/README.md`).

At 9600 GGA and RMC alone are 156 ms of line and RMC lands at about 226 ms, but the GSA that comes once a second makes the burst 333 ms, landing at about 403, past the quarter. At 115200 the widest fix burst lands at about 98 ms, and the widest GSV set on top of it at about 154. The specification asks for 115200 below 1000 ms anyway (V1.1 §2.3.2), so a Go whose part stays at 9600 is recovered rather than left at 1 Hz (`../../hardware/parts/l76k/README.md`, Recovery).

Nothing acknowledges `$PCAS01`, so the driver moves the rate only after `Verifying`, where the receiver has proved it obeys. `Confirming` then waits for a sentence that checksums at the new rate, and puts the port back if none comes. `port_can_retune()` sets the rate the port is already at, which `io::FixedUartRate` refuses, so a fixed port never moves the receiver. A factory reset returns the receiver to 9600, and the driver follows. The 2 Hz command comes after `Confirming`, once the part has answered at 115200.

`diagnostics` reports `baud`, `solution_phase_ms`, how long after its own quarter the last solution landed, and `overruns`, the UART reads the ring could not hold. The 2048-byte ring (`hardware/platform/zephyr/io.h`) holds 178 ms at 115200. If it overruns, grow the ring rather than slow the port.

## The satellites, and the second they do not fit in

GSV is the only sentence that carries signal level. `sky.h` keeps one entry per satellite: id, elevation, azimuth, C/N0, the constellation from the talker, and whether GSA used it. An empty C/N0 is a satellite in view but not tracked, kept as zero and drawn as a foot.

QZSS answers on the `GP` talker, and ids 193 to 197 tell it from GPS, as the L76K specification states. BeiDou ids restart at 1, so an id means nothing without its talker. GSA carries the system in field 18. Without it, the in-use mark matches the id alone. A GSA with an empty PDOP comes from a constellation in no solution, so it is skipped and does not overwrite the fix mode and VDOP.

GSV is asked for only while the `satellites` page is on the glass, the only reader of a level, because the widest set adds 648 bytes to a solution. `ScreenService` sets `bus::GnssState::levels_wanted`, and the board sends one `$PCAS03` with a null in every field but nGSV, which the receiver reads as keep the rest. The page opens empty and fills one solution later.

The part sends GSV between GSA and RMC, so GSV delays the fix: the widest set is 56 ms at 115200, out of the quarter the solution has to land in. So the levels are paced. nGSV is 1 until the first set lands, then `L76k::kHalfSecondSatellitesInViewPeriodSolutions`, one set in nine solutions, the slowest `$PCAS03` counts to, or `kSatellitesInViewPeriodSolutions`, one in five, on a receiver left at 1 Hz. At 2 Hz the bars move every 4.5 s while the counts and DOPs move every second.

## The settle, and why it is measured rather than waited out

Nothing is transmitted until the first fix settles. A cold receiver's first solutions walk, and ground speed is what `core/flight/state.h` calls a takeoff. A takeoff sets the transmit rate, starts the flight clock, opens the log and locks out firmware updates.

`FirstFix` settles on `kConvergedFixes`, three consecutive 3D solutions whose `prediction_residual_m` is under `kSettleResidualM`. The residual is how far the constant-speed, constant-turn model missed the new fix, an L1 sum in metres over one second. `kSettleResidualM` is `flight::kFlightSpeedMmS` over that second, so an error under it cannot invent a flight. One residual can be luck, hence three. A 2D solution never counts, because its altitude is not a measurement. Own-ship feeds the residual, the settle and the flight state only the first solution of each UTC second, so at 2 Hz they read as at 1 Hz, even with a `.000` lost or stamped a few milliseconds off (`products/common/services/ownship.cpp`).

The clock is a ceiling: `kFirstFixSettleMs` (20 s) after a first fix and `kRefixSettleMs` (5 s) after a re-fix. A receiver in rough air or with a poor antenna may never converge, and never transmitting is worse than a velocity a second behind.

`take_acquired()` is true once, on the first fix since boot, so the first-fix tune plays once.

`models::L76k::walk_amplitude_m` lets a case simulate a cold receiver: position error at the first solving second, alternating in sign each whole second and decaying over `walk_duration_ms`. It is zero by default.

## The rate

The Go solves twice a second, `L76k::kSolutionPeriodMs`, on `.000` and `.500`, two of the quarters G.1.1 counts (`go::kSolutionRate`). Bravo transmits nothing and solves once a second. The commands and what the bench still has to check are in `../../hardware/parts/l76k/README.md`. The rate lets a Traffic burst carry the newest solution as measured, named by its own quarter (G.1.1), and held once it would be over 500 ms old on air (G.1.16, `../timing/README.md`, The age of a position). At 1 Hz that holds most of the direct slot. At 2 Hz it holds only the positions drawn between 490 ms and the `.500` solution's arrival, on 868.2 MHz, and those are drawn again over the rest of their window. Whether G.1.1 wants every position measured on a quarter and sent within 500 ms is a question to EASA (`../../../docs/ADSL_EASA_QUESTIONS.md`). The extra solutions are noisier and do not shorten a cold start, so own-ship hands the flight model and the diagnostics record the whole-second solution alone. The specification pairs rates above 1 Hz with 115200 (V1.1 §2.3.2), and `L76k` static-asserts that the widest burst fits one solution period.

## PPS, and what it is worth

`timing::ClockState::pps_edge_us`, latched in a GPIO interrupt, is phase 0 of the UTC second the ADS-L dwell map is measured against. A solution is dated by its own UTC time, not by the arrival of its last sentence. Once a whole-second solution names the edge it follows, `ClockState` holds that edge and its UTC second, and `carry_utc_to_edge()` moves the pair on at every edge. `anchored_instant_ms` maps any solution through the pair, so a solution that lands late keeps the instant it was solved at. Own-ship drops an anchor that would put a solution after its arrival, and anchors again on the next whole second. A stale whole second maps to an older edge, so it never re-anchors the current one. Before the first anchor, `solution_instant_ms` takes the latched edge plus the stamp's milliseconds, moved by whole seconds to the latest such instant not after the arrival. Without a lock it subtracts `GnssSolution::pps_latency_ms`, zero until a driver stamps it.

Only a trusted anchor dates a fix. `ClockState::utc_anchor_fixed` is set when the anchoring whole second was a valid fix, stamped within 10 ms of the second, drained alone, and delivered inside the latency ceiling after its edge. The ceiling is `kSolutionLatencyCeilingMs`, 250 ms, or `GnssSolution::delivery_ms` plus a service pass when that is longer. `delivery_ms` is the part's start and line time at the live baud, 98 ms at 115200 and 403 ms at 9600, so a port stuck at 9600 still anchors the clock, and a delivery a second late is refused at either rate. A position needs a fix dated through a trusted anchor, `own.fix_dated`, and is held and counted in `air.tx_held_for_fix` until there is one. An anchor set by an unfixed stamp dates no fix: a valid fix is dated by its phase instead, and its whole second anchors the clock again. Only another fix inside its quarter replaces a trusted anchor. A stamp within G.1.1's 10 ms of a quarter is that quarter everywhere, `gnss::named_quarter_ms`: the Traffic timestamp, the anchor, and the whole second the flight log and the EFB read.

### What the device can and cannot see about a late solution

The driver empties the port before every configuration, so nothing that waited there before bring-up becomes a solution. It publishes the newest same-epoch pair a drain holds, and only if its stamp is later than the last one published, so a replay never goes out. A pair drained behind another is marked `backlogged` and never anchors the clock, because its arrival is when the host got to it.

The design assumes the L76K delivers each solution well under 250 ms after the epoch it names, and nothing on the board can check it. A receiver that delayed every solution by a second from power-up would look on time and anchor the clock a second young. The case `ADS-L.4.SRD860.G.1.16: an L76K that delivers every solution a second late is refused` is skipped as `unmeasured:` for that reason, and `../../hardware/parts/l76k/README.md` says how the bench measures it.

Whether the L76K pulses without a fix is not documented. Its chip, an AT6558, selects continuous output or output only while positioning in the CASIC `CFG-TP` `enable` field, which we never send. It matters because `timing::kPpsHoldoverMs` gives the slot map 60 s of dead reckoning after the edges stop. To settle it, cold-start the receiver indoors and watch whether `platform::zephyr::Pps::edges()` moves while the page reads `NO FIX`.
