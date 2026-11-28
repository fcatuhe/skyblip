# ag3335

The Airoha AG3335M on the Seeed MeshTracker X1: a dual-band receiver, L1 and L5, that skyBlip Bravo reads for its own position, UTC and PPS. `ag3335.h` is the driver, `pair.h` reads the receiver's `$PAIR` replies, and `model.h` with `model_sentences.h` is the host model. `../../platform/zephyr/ag3335.h` is the receiver's wiring on silicon. No X1 has run skyBlip yet, so every pin, rate, timing and command below is unverified until #169 benches a unit.

## The command document

Airoha publishes no `$PAIR` reference, and no public document describes the X1's firmware, `AG3335M_V3.3.0`. The driver relies on two:

- The SIMCom SIM65M Series NMEA Message User Guide, version 1.00, 21 July 2022, documents Airoha's `$PAIR` set. The SIM65M is L1 only (note to §2.3.36), and the guide names the AG3335 only among the values of `Project_board` (§2.3.11). Bare section numbers below are this guide's.
- Quectel's LC29H Series & LC79H (AL) GNSS Protocol Specification, version 1.4, 13 January 2025, is the dual-band reference, cited as LC29H V1.4. The LC29H carries an AG3335 (jclark/satpulse PR #522).

SoftRF (`src/driver/GNSS.cpp`, `src/platform/nRF52.cpp`, commit `d091924e`) and Meshtastic (`src/gps/GPS.cpp`, `variants/nrf52840/seeed_mesh_tracker_X1/`, commit `6d41e279`) are GPL-3.0 and were read for facts only.

## The rate

The product chooses the rate and the driver applies it: `Ag3335` takes a `ports::SolutionRate` at construction, from the board, from the product's `kSolutionRate`. The driver solves once a second, the `$PAIR050,1000` below, and nothing faster has been tried on an X1. `Ag3335::solves()` says so at compile time, and `common::solution_rate_serves` refuses a product that would ask more. Asked for more at run time anyway, the driver sends nothing, publishes no fix and reads `DEGRADED` from its first pass, so the fault shows on the self-test and in the dump's `config` instead of a unit quietly running at 1 Hz. Bravo asks one solution a second (`../../../products/skyblip_bravo/README.md`, The GNSS rate).

## Bring-up

The AG3335 acknowledges every command with `$PAIR001,<command>,<result>`: 0 is success, 1 is still processing, 2 to 5 are failures (§2.3.1). So the driver sends one command and waits for its acknowledgement before the next:

- A command with no answer inside `kAckWindowMs`, or with a failure, is sent again, at most `kCommandAttempts` times.
- A command that never succeeds is skipped, so one refusal leaves no later setting on its default, and the receiver reads `DEGRADED` once the sequence ends. A receiver that answers nothing ends there too.
- A result of 1 states no time to wait, so the driver waits `kProcessingWindowMs`, three acknowledgement windows, before resending.
- An acknowledgement names the command and not its arguments, so a late answer to a resent `$PAIR062` can be credited to the next `$PAIR062`.

The driver asks `$PAIR021` first. The reply carries the project version, such as `AG3335M_V3.3.0`, and a frequency field, `S` for single band or `D` for L1 and L5 (§2.3.11, §2.3.12), which the bench reads off the wire. The reply is 153 bytes in the guide's example, longer than an NMEA sentence, so `PairReader` keeps a line of its own beside `core/gnss`'s parser.

The sequence, in order:

| Sentence | What it sets | Source |
|---|---|---|
| `$PAIR382,1` | keeps the receiver's processor awake, so no later command is lost to sleep | §2.3.73. SoftRF sends it first on the X1 (`nRF52.cpp:4061-4067`) |
| `$PAIR066,1,1,1,1,1,0` | GPS, GLONASS, Galileo, BeiDou and QZSS | §2.3.36, LC29H V1.4 §2.4.15 |
| `$PAIR100,1,0` | NMEA 4.10, whose GSA carries the system id in field 18 | §2.3.61 |
| `$PAIR062,<type>,<every>` | GGA, GSA and RMC every solution. GLL, GSV, VTG and ZDA off | §2.3.32 |
| `$PAIR050,1000` | one solution a second | §2.3.26 |
| `$PAIR080,0` | normal navigation mode | §2.3.50, LC29H V1.4 §2.4.23 |
| `$PAIR400,2` | SBAS as the correction source | §2.3.78 |
| `$PAIR410,1` | search for SBAS satellites | §2.3.80 |
| `$PAIR752,1,100` | a 100 ms PPS from the first fix on | §2.3.141, LC29H V1.4 §2.4.48 |

**L5.** The SIM65M guide has no command for the band: an "L1 + L5 package" searches in GREB or GEB (§2.3.36). LC29H V1.4 documents `$PAIR104` and `$PAIR105` to set and read dual band (§2.4.29-30), only with the GNSS powered off and back on. Neither says which holds for `AG3335M_V3.3.0`. The driver sends `$PAIR066,1,1,1,1,1,0`, GREB with QZSS, which both documents allow and SoftRF sends outside India (`GNSS.cpp:1268-1274`). It sends no `$PAIR104`, which would power-cycle the GNSS mid bring-up on a firmware nobody has seen take it. The receiver restarts on `$PAIR066` for an unstated time, and the acknowledgement retry covers a restart shorter than `kCommandAttempts` windows.

**SBAS.** Mode 2 of `$PAIR400` covers WAAS, EGNOS, GAGAN and MSAS, and nothing names EGNOS alone, so the receiver takes whichever SBAS it hears. SBAS does not run in fitness or swimming mode (§2.3.80), and the guide calls both fitness (§2.3.50) and normal (§2.3.51) the default, so the sequence sets normal mode explicitly.

**Normal mode's limits.** LC29H V1.4 §2.4.23 Table 7 gives normal mode a ceiling of 10000 m and 100 m/s, 194 kt, and no mode goes faster. The L76K runs an aviation dynamic model, and the AG3335 documents none, so whether a fix holds above 194 kt or 10000 m is a flight check.

**PPS.** The guide gives no default pulse, so the driver sets one. Type 1 pulses after the first fix (§2.3.141, LC29H V1.4 §2.4.48). Types 2 and 3 stop with the fix, and type 4 pulses before the first fix too. Neither document says what type 1 does when the fix drops: we expect it to keep pulsing on the receiver's clock, unverified until the bench covers the antenna. The 100 ms width matches the L76K's. `timing` latches the rising edge, and an edge is still not a fix.

**Satellites in view.** Once the sequence ends, the driver sends `$PAIR062,3,1`, GSV every solution, while the board asks for it, and `$PAIR062,3,0` once it no longer does, forgetting the set. Bravo asks while it has no fix, so a capture tells a sky the antenna cannot see from a slow first fix: on bravo-d582 a first fix took 10 minutes and `satellites_in_view` read 0. GSV comes before RMC, so it delays the solution, which costs nothing without a fix, since Bravo exports nothing then. The commands go unacknowledged, as a restart does, and the sentences arriving or stopping say whether the receiver took them.

**What the sequence leaves out.**

- No `$PAIR513` save to flash (§2.3.103): the driver sends the sequence on every boot, and VRTC keeps the backup domain powered between boots. `board.c` raises VRTC's enable, P1.13, at every boot (`../../../boards/seeed/meshtracker_x1/README.md`, The way down).
- No autobaud: SoftRF (`nRF52.cpp:4022`) and Meshtastic (`variant.h:126`) both run the X1's port at 115200. It is also the lowest rate `$PAIR864` lists, and that command takes a reboot (§2.3.151).
- No `$PAIR154,0`, which SoftRF sends to turn RLM off (`GNSS.cpp:1311`) and the guide does not document.

## Timing

`kBurstStartMs`, 90 ms, is when SoftRF expects GGA after the second on this part (`GNSS.cpp:1342`). The driver's burst is GGA, one GSA for each of four constellations, and RMC. `kBurstBytes` counts each at NMEA 0183's 82-byte ceiling, 492 bytes or 42 ms at 115200. RMC closes the solution, as on the L76K: Meshtastic issue #4626 logs the order on a T1000-E's AG3335 as GGA, GLL, GSA, GSV, RMC, VTG and ZDA.

`pps_latency_ms()` is the start plus the line time, 132 ms. A solution is dated by the PPS edge when there is one (`core/gnss/nmea.h`, `solution_instant_ms`), and by its arrival less this latency when there is not.

## GGA field 11 (#162)

The guide documents GGA field 11 as the geoid separation (§2.2.1), and no log of a fix from an X1 confirms it yet. `core/gnss` reads it as the L76K's: a filled field sets `geoid_measured`, and an empty one falls back on `kDefaultGeoidSeparationMm` and says so.

A receiver with no fix sends a 37-byte GGA (Meshtastic issue #4626). `core/gnss` refuses a GGA under `kGgaLengthFloorBytes`, 40, so without a fix the refusal reads `NO GGA` rather than `NO SOLUTION`.

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

`Ag3335Wiring::begin()` drives four control lines in SoftRF's order (`nRF52.cpp:1865-1878`): enable, reset, sleep, RTC wake. The board raised the RTC supply before. It then opens the UART and arms the PPS. It reads them from the devicetree aliases `gnss-uart`, `gnss-pps`, `gnss-enable`, `gnss-reset`, `gnss-sleep` and `gnss-rtc-wake`.

## The model

`models::Ag3335` acknowledges `$PAIR` commands as the guide does and answers `$PAIR021`. It emits the sentences its rates ask for in the logged order, on the `GN` talker in NMEA 4.10, with the no-fix forms Meshtastic issue #4626 logged. Its factory state is all seven sentences once a second, as that issue reports. It flies the same `GnssTruth` as the L76K's model (`../gnss_truth.h`). Its GSV is one GPS set of `satellites_in_view`. It does not pulse a PPS, sleep or model a cold start, because nothing on Bravo reads them yet.

## What the bench checks (#169)

- Each pin above on the unit. P0.04 pulsing once a second settles SoftRF's PPS against Meshtastic's `VCC_ADC`.
- `$PAIR021` answers `AG3335M` and `D`, and `$PAIR105` answers whether the dual-band switch exists and is on (LC29H V1.4 §2.4.30).
- Every command is acknowledged with result 0, `$PAIR066` before the receiver restarts, and the restart's length. `$PAIR063,-1` reads back the seven sentence rates (§2.3.33).
- With a fix, GGA field 11 is filled and `geoid_measured` is true.
- GSV, turned on by hand, shows GPS on signal 8 (L5) and Galileo on signal 1 (E5a) (§2.2.3), and GGA reports fix quality 2 once SBAS is in use (§2.2.1).
- The PPS starts with the first fix and keeps pulsing with the antenna covered.
- When GGA and RMC land after the PPS edge, against `kBurstStartMs` and `pps_latency_ms()`.
- In flight, the fix holds in normal mode up to the aircraft's ceiling and fastest speed.
