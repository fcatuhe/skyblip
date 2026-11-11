# ag3335

The Airoha AG3335M on the Seeed MeshTracker X1: a dual-band receiver, L1 and L5, that skyBlip Bravo reads for its own position, UTC and PPS. `ag3335.h` is the driver, `pair.h` reads what the receiver says back in its own `$PAIR` sentences, and `model.h` with `model_sentences.h` is the host model. `../../platform/zephyr/ag3335.h` is the receiver's wiring on silicon. No X1 has run skyBlip yet, so every pin, rate, timing and command below is unverified until #169 benches a unit: that the receiver acknowledges each command, tracks L5 and uses SBAS.

## The command document

Airoha publishes no `$PAIR` reference of its own, and neither public document describes the X1's AG3335M firmware, `AG3335M_V3.3.0`.

- The SIMCom SIM65M Series NMEA Message User Guide, version 1.00, dated 21 July 2022, documents Airoha's `$PAIR` set. The SIM65M itself is single band: the guide's own note to §2.3.36 says "For SIM65M: L1 single frequency", and its example replies name an `AG3352Q`. It names the AG3335 only among the possible values of the `Project_board` field (§2.3.11: AG3335A, AG3335M, AG3335S, AG3352Q). Bare section numbers below are that guide's.
- Quectel's LC29H Series & LC79H (AL) GNSS Protocol Specification, version 1.4, dated 13 January 2025, is the dual-band reference. jclark/satpulse PR #522 speaks of "the AG3335 in the LC29H". Its sections are cited as LC29H V1.4. SoftRF (`src/driver/GNSS.cpp`, `src/platform/nRF52.cpp`, commit `d091924e`) and Meshtastic (`src/gps/GPS.cpp`, `variants/nrf52840/seeed_mesh_tracker_X1/`, commit `6d41e279`) are GPL-3.0 and were read for facts only.

## Bring-up

Unlike the L76K, the AG3335 acknowledges every command: `$PAIR001,<command>,<result>`, where 0 is success, 1 is still processing, and 2 to 5 are failures (§2.3.1). So the driver sends one command, waits for its acknowledgement, and only then sends the next. A command with no answer inside `kAckWindowMs`, or with a failure, is sent again, at most `kCommandAttempts` times. A command that never succeeds is skipped, so one refusal leaves no later setting on its default, and the receiver reads `DEGRADED` once the sequence ends. A receiver that answers nothing at all ends there too. A result of 1 asks the host to wait for the final result (§2.3.1) and states no time, so the driver waits `kProcessingWindowMs`, three acknowledgement windows, before it sends the command again. An acknowledgement names the command and not its arguments, so a late answer to one `$PAIR062`, after the driver has resent it, can be credited to the next `$PAIR062`. The bench reads the rates back with `$PAIR063` (§2.3.33).

The driver asks `$PAIR021` first. The reply carries the project version, such as `AG3335M_V3.3.0` on an X1 in Meshtastic PR #11889, and a frequency field, `S` for single band or `D` for L1 and L5 (§2.3.11 and §2.3.12). `dual_band()` reads it. The reply is 153 bytes in the guide's L1-only example, longer than an NMEA sentence, so `PairReader` keeps a line of its own beside `core/gnss`'s parser.

The sequence, in order:

| Sentence | What it sets | Source |
|---|---|---|
| `$PAIR382,1` | keeps the receiver's processor awake, so no later command is lost to sleep | §2.3.73. SoftRF sends it on the X1 before anything else (`nRF52.cpp:4061-4067`) |
| `$PAIR066,1,1,1,1,1,0` | GPS, GLONASS, Galileo, BeiDou and QZSS | §2.3.36, LC29H V1.4 §2.4.15 |
| `$PAIR100,1,0` | NMEA 4.10, whose GSA carries the system id in field 18 | §2.3.61 |
| `$PAIR062,<type>,<every>` | GGA, GSA and RMC every solution. GLL, GSV, VTG and ZDA off | §2.3.32 |
| `$PAIR050,1000` | one solution a second | §2.3.26 |
| `$PAIR080,0` | normal navigation mode | §2.3.50, LC29H V1.4 §2.4.23 |
| `$PAIR400,2` | SBAS as the correction source | §2.3.78 |
| `$PAIR410,1` | search for SBAS satellites | §2.3.80 |
| `$PAIR752,1,100` | a 100 ms PPS from the first fix on | §2.3.141, LC29H V1.4 §2.4.48 |

**L5.** The two documents say different things. The SIM65M guide has no command for the band: it says an "L1 + L5 package" searches in GREB or GEB, with QZSS switchable in either (§2.3.36). LC29H V1.4 documents a switch, `$PAIR104` to set dual band and `$PAIR105` to read it (§2.4.29-30), for the LC29H(AA) and LC79H(AL) only, set with the GNSS powered off by `$PAIR003` and back on by `$PAIR002`. Its §2.4.15 lists GPS, GLONASS, Galileo, BDS and QZSS among the sets those two modules take. Neither says which holds for `AG3335M_V3.3.0`. The driver sends `$PAIR066,1,1,1,1,1,0`, GREB with QZSS, which both documents allow and SoftRF sends outside India (`GNSS.cpp:1268-1274`). It sends no `$PAIR104`, which would power the GNSS off and on in the middle of bring-up on a firmware nobody has seen take it. It reads the `D` of `$PAIR021` as the package, and the bench asks `$PAIR105`. The guide says the receiver restarts on `$PAIR066` and states no duration, so the commands after it may go unanswered for a while. The acknowledgement retry covers a restart shorter than `kCommandAttempts` windows.

**SBAS.** Mode 2 of `$PAIR400` covers WAAS, EGNOS, GAGAN and MSAS, and the guide offers no way to name EGNOS alone, so the receiver takes whichever SBAS it hears. SBAS does not run in the fitness or swimming navigation modes (§2.3.80), and §2.3.50 lists fitness as the default, so the sequence sets normal mode explicitly. §2.3.51 calls normal the default instead. The guide contradicts itself, and the sequence does not depend on which is right.

**Normal mode's limits.** LC29H V1.4 §2.4.23 Table 7 gives normal mode a maximum altitude of 10000 m and a maximum speed of 100 m/s, 194 kt. No mode in that table goes faster: fitness, drone and bike stop at 30 m/s. The SIM65M guide gives no limits. The L76K was put in an aviation dynamic model on purpose, and the AG3335 documents none, so whether a fix holds above 194 kt or 10000 m on `AG3335M_V3.3.0` is a flight check.

**PPS.** The guide gives no default for the pulse, so the driver sets one rather than run on a default nobody states. Type 1 pulses "after the first fix" (§2.3.141, LC29H V1.4 §2.4.48). Neither document says what it does when the fix drops. We expect it to keep pulsing on the receiver's own clock, so the slot map's holdover would not start the moment a fix drops, and that stays unverified until the bench covers the antenna. Types 2 and 3 stop the pulse with the fix, and type 4 pulses always, before the first fix too. The 100 ms width matches the L76K's pulse. `timing` latches the rising edge, and an edge is still not a fix.

**What the sequence leaves out.** `$PAIR513` saves the settings to flash (§2.3.103). The driver sends the sequence on every boot, and the VRTC line keeps the backup domain powered between boots, so nothing needs saving. SoftRF (`nRF52.cpp:4022`) and Meshtastic (`variant.h:126`) both open the X1's port at 115200 and never retune it, so the driver opens it there and has no autobaud. 115200 is also the lowest rate `$PAIR864` lists, and that command takes a reboot (§2.3.151). SoftRF also sends `$PAIR154,0` to turn RLM off (`GNSS.cpp:1311`). The guide does not document `$PAIR154`, and the parser ignores any sentence it does not know.

## Timing

`kBurstStartMs`, 90 ms, is when SoftRF expects GGA after the second on this part (`GNSS.cpp:1342`, `90 /* GGA */, 100 /* RMC */`), with GSA off in SoftRF's configuration. The burst the driver asks for is GGA, one GSA for each of four constellations, and RMC: `kBurstBytes` counts each at NMEA 0183's 82-byte ceiling, 492 bytes or 42 ms at 115200. RMC closes the solution, as on the L76K: Meshtastic issue #4626 logs the order on a T1000-E's AG3335 as GGA, GLL, GSA, GSV, RMC, VTG and ZDA.

`pps_latency_ms()` is the start plus the line time, 132 ms. A solution is dated by the PPS edge when there is one (`core/gnss/nmea.h`, `solution_instant_ms`), and by its arrival less this latency when there is not. The L76K counts the line time alone and leaves its 70 ms start out.

## GGA field 11 (#162)

The guide documents GGA field 11 as the geoid separation, with `Ellipsoid altitude = MSL Altitude + Geoid Separation` and `7.9` in its example (§2.2.1). Meshtastic issue #4626 logs the field empty on a receiver with no fix, which says nothing about a fix. So the AG3335 is documented to fill field 11, and no log of a fix from an X1 confirms it yet. `core/gnss` reads it as the L76K's: a filled field sets `geoid_measured`, and an empty one falls back on `kDefaultGeoidSeparationMm` and says so. The bench reads `geoid_measured` on a fix.

The same log shows a receiver with no fix sending `$GNGGA,000008.012,,,,,0,0,,,M,,M,,`, 37 bytes. `core/gnss` refuses a GGA under `kGgaLengthFloorBytes`, 40, so without a fix the reason a solution is refused reads `NO GGA` rather than `NO SOLUTION`. The fix itself is unaffected.

## Pins

| Line | X1 pin | Level | Sources |
|---|---|---|---|
| UART, nRF52 TX to the receiver | P0.13 | 115200 8N1 | SoftRF `Seeed_X1.h:8`, Meshtastic `variant.h:86` |
| UART, the receiver to nRF52 RX | P0.14 | | SoftRF `Seeed_X1.h:7`, Meshtastic `variant.h:85` |
| PPS | P0.04 | rising edge | SoftRF `Seeed_X1.h:10`. Meshtastic's `variant.h:143` calls P0.04 `VCC_ADC` instead |
| Enable | P1.11 | high to run | SoftRF `Seeed_X1.h:11`, Meshtastic `variant.h:129-130` |
| Reset | P0.08 | active high, held low | SoftRF `Seeed_X1.h:12`, Meshtastic `variant.h:132-133` |
| RTC supply (VRTC) enable | P1.13 | high | SoftRF `Seeed_X1.h:13`, Meshtastic `variant.h:135` |
| Sleep | P0.30 | high | SoftRF `Seeed_X1.h:14`, Meshtastic `variant.h:136` |
| RTC wake | P0.29 | low, high wakes | SoftRF `Seeed_X1.h:15`, Meshtastic `variant.h:137` |

`Ag3335Wiring::begin()` drives the five lines in SoftRF's order (`nRF52.cpp:1865-1878`): enable, RTC supply, reset, sleep, RTC wake. It then opens the UART and arms the PPS. It reads them from the devicetree aliases `gnss-uart`, `gnss-pps`, `gnss-enable`, `gnss-rtc-enable`, `gnss-reset`, `gnss-sleep` and `gnss-rtc-wake`, the names #170 gave them in the X1's devicetree. `platform/zephyr/meshtracker_x1.h` holds the wiring and `boards/seeed/meshtracker_x1/board.h` the driver, for skyBlip Bravo. `test/twister/ag3335` builds both. Its overlay declares the same nodes under the same names as #170's devicetree, so it builds on the stub board and adds nothing on #170's.

## The model

`models::Ag3335` takes `$PAIR` commands checksum first, acknowledges each as the guide does, and answers `$PAIR021`. A case can make it drop acknowledgements, refuse a command, restart on `$PAIR066`, or answer one command with result 1 before its final result. It emits the sentences its rates ask for in the logged order, on the `GN` talker in NMEA 4.10, with the no-fix forms Meshtastic issue #4626 logged. Its factory state is all seven sentences once a second, as that issue reports. It flies the same `GnssTruth` as the L76K's model (`../gnss_truth.h`), so a scenario moves either receiver the same way. It does not emit GSV, pulse a PPS, sleep or model a cold start, because nothing on Bravo reads them yet.

## What the bench checks (#169)

- The pins above, each against the unit: the receiver answers on P0.13 and P0.14 at 115200, and P0.04 pulses once a second, which settles SoftRF's PPS against Meshtastic's `VCC_ADC`.
- `$PAIR021` answers `AG3335M` and `D`, and `$PAIR105` answers whether this firmware has the dual-band switch and whether it is on (LC29H V1.4 §2.4.30).
- Every command of the sequence is acknowledged with result 0, and `$PAIR066` is acknowledged before the receiver restarts. Measure how long the restart lasts. `$PAIR063,-1` reads back the seven sentence rates the sequence set.
- With a fix, GGA field 11 is filled, `geoid_measured` is true, and the diagnostics show the fix, UTC, PPS lock and HAE.
- GSV, turned on by hand, shows GPS satellites on signal 8 (L5) and Galileo on signal 1 (E5a) (§2.2.3), and GGA reports fix quality 2 once SBAS corrections are in use (§2.2.1).
- The PPS starts with the first fix and keeps pulsing when the antenna is covered.
- When GGA and RMC land after the PPS edge, against `kBurstStartMs` and `pps_latency_ms()`.
- In flight, unverified until then: the fix holds in normal mode up to the aircraft's ceiling and its fastest speed, against Table 7's 10000 m and 100 m/s (194 kt).
