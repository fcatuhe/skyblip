# core/flight

What the aircraft is doing, decided from the fix stream and the barometer. Pure, testable, and free of every framework header: the services in `products/` feed these and publish what they answer.

| File | What it decides |
|---|---|
| `state` | airborne or on the ground, which gates the DFU lockout and the transmit rate, and stopped or rolling while it is on it |
| `ground` | the same answer with a landing held back, which is what a permission gate reads |
| `timer` | how long this flight has been running |
| `atmosphere` | the standard atmosphere as integer math: pressure altitude, vertical speed |
| `turn` | rate of turn from two reported tracks |
| `indicated` | what the glass shows of the climb and the turn: the same rates, damped |
| `force` | the specific force the case measures, in the axes a pilot names, and what every reading taken from it shares |
| `slip` | where the ball hangs, from the acceleration the case measures |
| `gload` | what the airframe is pulling, now and at its worst |
| `extrapolate` | where an aircraft is now, when the fix it came from is older than now |
| `arc` | where an aircraft will be, out to the look-ahead the radar draws |
| `log_record`, `log_session`, `log_events` | what a flight leaves behind, when a session runs, and what was around it |

## Measured for the glass, sent as measured

The climb and the turn exist twice: the measurement, in `state.own`, and a damped copy for the glass, in `state.indicated`. The ball is damped for the glass alone. ADS-L, the residual, `$LK8EX1`, the logs and the `status` page read the measurement. Only the six-pack and own-ship's leader on the radar read the damped copy. A receiver two kilometres away needs what the aircraft is doing now, and a tablet's vario damps by its own setting, so nothing in this directory's filters may sit on the way to `core/protocol`.

## atmosphere

Pressure gives a vertical rate that needs no agreement with anyone, and that is what this module is for. It is not the altitude skyBlip broadcasts: the alarm compares relative altitudes (`core/traffic/alarm.h`), so every participant must share the datum ADS-L names.

The curve is the ICAO troposphere formula tabulated at 500 Pa steps and interpolated, in millipascals to millimetres, so the chain never loses what the part resolves:

| Link | Resolution | As altitude |
|---|---|---|
| BME280 forced 4 times a second, x16 / x2 oversampling and IIR 4 | 0.66 Pa RMS noise | 5.5 cm |
| what `parts::Bme280` compensates to (DS 4.2.3, Q24.8) | 1/256 Pa | 0.3 mm |
| SPA06 once a second at x64, on the X1, the datasheet's figure until #169 measures it | 0.3 Pa RMS noise | 2.5 cm |
| what `parts::Spa06` compensates to (DS 4.6.1, Q16) | 1/65536 Pa | 0.001 mm |
| `pressure_to_altitude_mm` | 1 mPa | 0.008 mm |
| table interpolation against the closed form | | under 25 cm, a bias, not jitter |
| `to_climb_e8`, the ADS-L G.1.9 unit | 0.125 m/s | 24.6 ft/min of rate |

A whole pascal would add 17 ft/min of invented rate over a one-second window. The rate is measured in mm/s and encoded to eighths of a metre per second only where the radio needs it (`core/protocol/adsl.cpp`).

`kClimbWindowFloorMs` and `kClimbWindowCeilingMs` bound the interval a rate may be taken over: too short and the sensor noise is the answer, too long and it is history. In `products/common/services/ownship`, `kBarometerClimbWindowMs` is the floor, one second, and `kGnssClimbWindowMs` four times that, because a GNSS height jitters by metres. A barometer, once it has spoken, owns the rate, and the GNSS rate keeps running underneath so losing the part falls back without a step.

The barometer converts four times a second on the PPS quarters and publishes the conversion triggered on the edge (`boards/lilygo/t_echo_plus/board.h`), so a rate covers the second the fix stream is dated in. Its delays are the same every second, so the difference is still the true rate (`hardware/parts/bme280/README.md`).

## force

One accelerometer feeds two instruments, the ball and the g-meter, and what they share lives here so the two never give two answers about one sensor.

- `kResultantFloorMg`, 200 mg, is the magnitude below which a specific force cannot be divided by. `slip` refuses below it.
- `kLevelFlightMg` is one g, the g-meter's datum and the unit the pages print in tenths of.
- `kIndicatedStaleMs`, 2 s, is how long a reading off the hub stands after its last sample. The hub is drained at 12.5 Hz, so that is 25 missed samples, and a part that stopped answering takes its instrument off the glass rather than freezing it.
- `kIndicatedDampingSamples`, four samples, about a third of a second, is the glass's filter length. A step settles within a pixel in 800 ms, before the next one-second frame. `status` prints `SlipBall::measured_mg`, the undamped sample.

## slip

The turn coordinator's ball, from the BHI260AP (`hardware/parts/bhi260/`).

- A ball hangs along the resultant of gravity and the aircraft's acceleration, so it shows the lateral specific force **as a fraction of that resultant**. The same rudder mistake in a 2 g turn moves the ball half as far. `slip_from_specific_force` is that fraction in thousandths of g, the unit `sixpack` draws with.
- `kSlipFullScaleMg` is the full travel the cage allows, and the page reads it from here, because the cage and the ball are one instrument.
- The ball's deflection is minus the lateral force: a case accelerating left measures a leftward force, and the ball goes right. So the page's `lateral_mg` is a ball position, never an accelerometer reading.
- Below `kResultantFloorMg` there is nothing to hang from, and the reading is refused rather than scaled.
- `SlipBall` damps the position over `kIndicatedDampingSamples`, so e-paper is not refreshed for jitter. The force is never damped, because `GMeter` holds peaks and a filtered peak is one the airframe never pulled.

## turn

`turn` differentiates the GNSS track, a cordic9 word of 45/64 of a degree, over a second. So the smallest turn it expresses is 0.7 deg/s, the figure is a second old, and a changing wind moves it. The hub's gyroscope is not read, because it costs the better part of a milliamp (`hardware/parts/bhi260/README.md`).

`kTurnWindowMs` is the shortest window a 1 Hz track says anything over. It lives here because own-ship (`products/common/services/ownship`) and a target's history (`core/traffic/table`) both differentiate a track with it.

The rate is zero whenever `FlightMonitor::rolling()` is false, and restarts from the first fix that rolls, because below walking pace the track is receiver noise.

`own.turn_cdps` carries hundredths. `own.turn_dps` rounds to whole degrees for the residual and the alarm's arcs.

## indicated

`IndicatedRate` is the needle's damping, a first-order lag, as a vertical speed indicator's capsule and leak are. A BME280 differenced over one second jitters by about 12 ft/min at rest (6.2 and 6.6 cm/s RMS measured by build 895 on two units), so an undamped page would change on every frame.

`kIndicatedRateDampingMs` is its time constant, 2 s, for both rates, because they arrive at the same cadence on the same page. Each reading moves the needle `dt / (dt + damping)` of the way, a third at one reading a second, so a step shows 70% after three seconds and 94% after seven, and the barometer's 12 ft/min of noise reads as 3. The damping is in milliseconds, so the GNSS climb, taken over two seconds, settles in the same time. The ball's filter is in samples instead, because the hub hands it one every 80 ms without fail.

A climb that stops being valid resets the damping, so the next one is shown as measured.

## gload

`GMeter` holds the largest and smallest of each axis, in thousandths of g, with no fusion and no fix required. Each axis takes the instrument's sign, not the accelerometer's:

- Normal load is the load factor every g-meter shows: positive up, 1 g in level flight. The page calls it `G`.
- Lateral follows the ball, minus the measured force, so the two instruments never disagree.
- Longitudinal is the flight-test `Ax`, positive accelerating forward. The page draws it nose up, labelled `DEC` and `ACC`, so the marker sits where the load throws you.

The peaks reset when the flight timer starts, so they are this flight's.

## arc

`extrapolate` and `arc` are the same model over two horizons, and stay two functions. `extrapolate` carries own-ship and a neighbour to one instant for the alarm and the radar, where every 100 ms at 50 m/s is 5 m of error in a separation. `kExtrapolationCeilingMs` is a little over a second, because a neighbour reports once a second and an ALP-TAS frame leaves up to 1.2 s after its fix. Past it the last fix stands where it was, never at an invented position. Own Traffic is never carried: it goes on air as the receiver solved it (`core/protocol/README.md`). `arc` draws the radar's leader line, runs to a minute, and owes the radio nothing.

The model is OGN's (nrf52-ogn-tracker `src/ogn.h:1735-1790`): constant ground speed on a constant turn rate, the turn applied half before the step and half after, which keeps a circling aircraft on its arc instead of on the tangent. `Arc::advance()` walks the path, two sine lookups per aircraft per minute. A step is whole seconds and the position is `int32_t` millimetres, so a step takes no 64-bit division. That range holds the fastest speed an observation carries, 16,384 m/s, for two minutes. Rotations are rounded, because a truncated Q14 rotation applied thirty times loses a metre a step.

ADS-L carries no turn rate, so the radar page (`products/skyblip_go/pages/radar_traffic.cpp`) flies a target on the rate `core/traffic` estimated from its track history, and straight with no estimate yet. The arc is flat: the radar draws no climb.

`kTurnCeilingDps` is 30. A differentiated 1 Hz track produces tens of degrees per second out of a bad fix, and no aircraft this device rides in holds that rate for the length of the projection. `Arc` applies the clamp when it is built, so no caller has to remember it.

## state

Airborne or on the ground, from the ground speed alone. It gates the DFU lockout and the transmit rate, and the two errors are not symmetric: a false takeoff locks updates on the ground and drains the cell at the airborne rate, while a missed one leaves an aircraft transmitting at the parked rate, unlocked, in the air.

The receiver solves four times a second, and own-ship hands the monitor only the solution on each whole second (`products/common/services/ownship.cpp`). So every hold and gate below counts solutions a second apart.

| | Condition | Constant | Meaning |
|---|---|---|---|
| flight | ground speed >= 12.0 m/s | `kFlightSpeedMmS` | a speed no aircraft taxis at |
| landing | ground speed < 8.0 m/s for 10 s, airborne | `kLandingSpeedMmS`, `kLandingHoldMs` | slower than a taxi for longer than any circle |
| ground | ground speed < 1.0 m/s | `kGroundSpeedMmS` | stopped |
| rolling | ground speed >= 1.5 m/s, on the ground | `kRollingSpeedMmS` | the word TAXI rather than GROUND |
| between | keep the state you had | | |

There is no altitude, vertical rate or barometer in the rule, so it rests on one number the receiver solves on every fix. An MSL altitude rule would read airborne while parked at Samedan (1707 m) or Leadville (3026 m).

### Takeoff, 12.0 m/s

- 12.0 m/s is 23 knots. Under it are a taxi at 5 to 8 m/s and a tug hurrying back to the grid at 10, the fastest thing on an airfield that is not flying. Over it are a glider on aerotow at 20 and a light aircraft rotating at 28.
- Lower buys little: at about 2 m/s gained each second, 10 m/s would declare the takeoff two and a half seconds earlier, still on the runway.
- Higher fails into wind: liftoff is an airspeed, and a 120 kg class microlight into an 8 m/s breeze crosses the fence at 9 m/s over the ground.
- The gap to the 1.0 m/s ground band is the hysteresis. A glider thermalling in a 15 m/s wind swings from 5 to 40 m/s over the ground every circle, and one threshold in the middle would land and launch it once a turn.

A paraglider, a hang glider on a ridge or a balloon may never reach 12 m/s over the ground, and reads on the ground from launch to landing. `announced_state` sends on-ground as G.1.2's Undefined for every G.1.3 category that can fly at no ground speed, which G.1.16 transmits at the full rate, and the flight log opens on `rolling` for them (`log_session`). No vertical rate decides a takeoff, because on this board it is a gust or a pressure step as often as a climb.

### Ground, 1.0 m/s, and rolling, 1.5 m/s

A receiver at a standstill reports 0.05 to 0.3 m/s of Doppler speed, spiking to 1 m/s on multipath, so 1.0 m/s is three times the noise it sits above. Lower and a finished flight keeps running. Higher and an aircraft creeping on an apron reads parked.

`FlightMonitor::rolling()` is not a G.1.2 state: it decides GROUND against TAXI on the glass (`products/skyblip_go/pages/README.md`). It picks up at 1.5 m/s and lets go below 1.0, so a parked receiver's multipath spike never flips the word. It holds through an outage, and `products/common/services/ownship.cpp` publishes it as `state.flight.rolling`.

### Landing, 8.0 m/s for 10 s

A landing is a stop, declared on the solution that shows it, or a taxi held under 8.0 m/s for 10 s, so a landing reads TAXI before the aircraft comes to rest. 8.0 m/s sits above a taxi and 4 m/s clear of the takeoff. 10 s is what no circle can fake: a glider at 22 m/s airspeed in a 20 m/s wind is under 8 m/s over the ground for three seconds of a 25-second turn, and the hold restarts on the first solution above the ceiling. A glider beating a ridge into a 15 m/s wind can still read landed after 10 s, and the next solution at 12 m/s puts it back in the air.

### Evidence

- Evidence for flight is divided by the fix's dilution of precision, as OGN does, so an untrustworthy solution cannot declare a takeoff. Evidence for the ground is not derated, because a smaller figure must never be what lands an aircraft.
- The jerk gate refuses a speed that jumps more than fourfold between two solutions a second apart, a receiver at a standstill. It costs one sample at the start of every roll.
- The first solution after power-on decides on its own evidence, because `Unknown` would hand a unit rebooted in flight the wrong transmit rate and lockout. Between the bands it decides for the ground, the common case of switching on while towed to the grid.
- An aircraft that stops on the runway and rolls again past 12 m/s is a second takeoff and a second log session.

## ground

The bus carries `state`'s answer as the ADS-L G.1.2 code in `own.flight_state`. `state_from` is the only reader, and a code this build does not name is `Unknown`, never a state the sender did not say.

`report_period_s` maps the same two bits to the G.1.16 rate: `kAirborneReportPeriodS`, 1 s, and `kGroundReportPeriodS`, 10 s. Own-ship's schedule reads it (`core/timing/transmit.h`), and so does `core/traffic/lease.h`, which counts a neighbour's silence in its own intervals. An `Unknown` target gets the airborne period, because an emitter that does not say is heard at 1 Hz or not at all.

`GroundLatch` answers whether the device may be written, erased or updated. Only `Airborne` shuts the door. `Unknown`, no fix since boot, sends no position and grades no traffic, so it shuts nothing (`core/comms/README.md`, The flight gate). Once `Airborne` has been seen, only a positive `Ground` clears it, so a receiver dropping out over a ridge never unlocks an update mid-flight. There is one latch, in `products/common/services/ownship.cpp`, published as `state.flight.confirmed_state`, and the companion link's flight gate and the flight log's offload gate both read it.

## timer

`FlightTimer` counts from the takeoff `state.h` declares and from nothing else, so the clock, the log session and the transmit rate agree on when the flight began.

- It freezes at a landing and carries on at the next takeoff: it is airborne time since switch-on, so a touch and go is one logbook line and a false landing costs only its own seconds. Only switching off clears it.
- `Unknown` is neither a landing nor a pause: a flight already running keeps counting through an outage, as `FlightMonitor` holds its state.
- `flown()` tells no flight yet, drawn as dashes, from a flight zero minutes old, drawn `0:00`. The page reads `running()`, not `own.flight_state`, which reports `Unknown` on every bad solution.
- A unit switched on in the air is timed from its first solution, because `FlightMonitor` declares `Airborne` at once.
- The count is a difference of unsigned milliseconds, so the 49.7-day wrap of `ports::Clock::milliseconds()` is harmless.

## log_record

A record is 24 bytes and a sector holds 170 of them behind a 16-byte label. The label, and which sector a ring gets next, belong to [`core/store`](../store/README.md). Every field is written one at a time, little-endian, so the host reader sees no hidden padding. The device never writes IGC: the host converts the records.

Byte 16 tells a fix from an event. A fix carries its track's high bit there, so the byte is 0 or 1. An event sets the top bit and names its kind in the other seven. Bytes 0-1 and 22-23 are the same in both, so recovery, the torn-tail check and the offload never ask which a slot holds. `decode_log_record` answers `Unsupported` for an event, and the host suite's `decode_log_event` (`test/support/log_event_read.h`) answers it for a fix. A kind a later build adds is refused. A kind is retired, never renumbered, because a flight outlives the firmware that wrote it.

| Byte | Fix | Status | Alarm, encounter |
|---|---|---|---|
| 0-1 | seconds since the session opened | the same | the same |
| 2-5 | latitude, 1e-7 deg | cell mV (2-3), percent, power level | address (2-4), address table |
| 6-9 | longitude, 1e-7 deg | charge, aircraft heard, the most heard since the last status, flags | distance m, relative altitude m |
| 10-11 | altitude MSL, m | | bearing from own track, deg |
| 12 | ellipsoid minus MSL, m | | closing m/s (12-13) |
| 13-14 | ground speed, G.1.8 | | alarm level (14) |
| 15-16 | track, G.1.10 nine bits | `0x81` in 16 | aircraft category (15), `0x82` alarm or `0x83` encounter (16) |
| 17-18 | climb, G.1.9 | | source, RSSI dBm |
| 19-20 | satellites, HDOP tenths | | flags (19), seconds heard (20-21, encounter) |
| 21 | flags and the G.1.2 flight state | | |
| 22-23 | CRC-16-CCITT over 0-21 | the same | the same |

- The seconds since the session opened saturate at 65535, about 18 hours, rather than wrap into the past.
- Byte 12 fits in one signed byte because EGM96 runs from -107 m to +85 m over the planet.
- A `bravo` record, `0x84` in byte 16, carries in byte 2 the `comms::BravoState` code a paired Go made of its Bravo, the link registry's `bravo` enum with 0 for unpaired. Every other byte up to the CRC is 0.
- A target's flags are bit 0 dismissed, bit 1 in formation, bit 2 altitude recorded and bit 3 relative altitude valid. A target with no altitude writes 0 m, and bit 3 says whether those bytes are a separation. Bit 2 is set on every target this firmware writes, so a reader of an older flight knows its 0 m may be unknown.

The enum codes are the diagnostics capture's, pinned by the same tests (`../diagnostics/README.md`). [`schemas/flight_log.v1.schema.json`](../../../schemas/flight_log.v1.schema.json) is one decoded record, and `scripts/test_blip.py` holds the host decoder to it.

## log_events

`LogEvents` writes what was around the flight and what the cell did, in the same session and sectors as the track, so one read offloads both and one eviction loses both. It samples with the fix, once a second while a session is open, so every event is dated by a second the track holds.

| Kind | When | What |
|---|---|---|
| status | the first second of the session, every `kLogStatusPeriodMs` after it, and whenever the cell changes level | cell voltage, percent, power level, charge condition, aircraft heard now and the most heard at once since the last one |
| alarm | a contact's alarm level changes, either way | the contact and its geometry at that second |
| encounter | a contact leaves the table, another aircraft takes its slot, or the flight ends | the contact at its closest pass, dated at that pass, with the highest level it reached and how long it was heard |
| bravo | a paired Go's Bravo is lost, including on the first second of the session, and when it is no longer lost | the state, a reason when lost (`../comms/README.md`, What the Go makes of its Bravo) and `live` or `unpaired` when back, or `waiting` when back from a Bravo that has not sent UTC since the Go booted or paired |

- The table has no ADS-B gate (`../traffic/README.md`, No gate), so on a Bravo, and on a Go paired with one, every airliner that takes a slot writes an encounter. Its distance saturates at 65535 m.
- The geometry is `traffic::assess` and the level is the one the alarm service published, so the log, the buzzer and the tablet agree. A target with no valid position is not tracked.
- Events drain before fixes, so the landing's `session_end` fix stays the last record of the file, and its absence is how `list` tells a flight the power ended. Records are in write order, and an encounter is dated at its closest pass, so a reader sorts by `utc`.
- `kLogEventBacklogRecords` holds sixteen events for a writer that drains every pass. An overflow loses the oldest and counts it.

## log_session

A session opens on `Airborne` and closes on the landing, so the log and the transmitter agree on when the aircraft flew, and a unit parked on a trailer writes nothing.

The log reads `state.own_second`, own-ship as the receiver's last whole-second solution left it, stamped within 10 ms of `.000`. A fix or UTC withdrawn on `.500` withdraws it too. So a record holds the position of the second it is dated by, and a second whose `.000` solution was lost writes no record.

- `kLogRecordPeriodMs` is one second, the ADS-L G.1.16 airborne rate, so an incident is reconstructed on the second the radio spoke in. It costs 86.4 KB a flight hour, so the 298 sectors hold 14 hours (`../store/README.md`).
- `kLogPreTakeoffRecords` holds sixty records in RAM, a minute at one a second, and hands them to the file when it opens. A takeoff is declared at 12 m/s, partway down the runway, so the ring puts the roll back. The ground is not logged at its 10 s rate: every tug run would open a file, and flash cannot take one back. The ring holds the taxi before a takeoff, and the landing's ten-second hold the taxi after it. It holds each fix as its UTC second and the 20 wire bytes that carry no time, 24 bytes in RAM, and writes the offset from the session's base second and the CRC when the writer takes it.
- On the ground the ring is meant to turn over. Only a record the writer owed and did not take counts as a loss.
- For a G.1.3 category that flies at no ground speed, `flies_at_no_ground_speed`, a session opens on `rolling` too, and closes once the aircraft has stood still for `kLogStillHoldMs`, two minutes. Its full transmit rate is not the trigger, because a wing packed away on a shelf would log until the cell died. A wing hovering under 1 m/s for two minutes splits its flight into two files and loses nothing.
