# core/diagnostics

The diagnostics capture: one fixed-size record per thing the device sensed or decided, buffered in RAM while a capture is armed, so a bench or a flight can be replayed on a laptop and the tuning constants re-derived from what happened.

Every threshold in this tree, 12.0 m/s for a takeoff, 1000 ms for a long touch, 60 s of PPS holdover, the dwell map itself, was chosen from a datasheet, a reference project or an argument. A corpus turns that into measurement, and it is only worth having if it is comprehensive and its holes are visible.

## What it decides

`record.h` is the envelope: a 24-byte slot, the type tag, the instant, and the flags every type shares. `layout.h` holds the type codes, each field's payload offset and flag bit, and the enums this layer declares, all generated from the layout (below). `payload.h` is one struct per subject, each inside the 16 bytes the envelope leaves, encoded in `payload_sensed.cpp` for what reached the device and `payload_decided.cpp` for what it made of it. `recorder.h` is the RAM ring a capture fills and a writer drains.

| Not here | Where |
|---|---|
| the flash writing, the sector ring, the recovery scan | `core/store/`, and the service that owns the partition |
| the CRC over a record | the sector label's, `core/store/sector.h`: this geometry has no room for one, and the `End` marker below answers the torn tail |
| the BLE offload and the chunking | `core/comms/log_link.h`, shared with the flights store |
| the taps that fill these records | the services in `products/`, one call each, behind `armed()` |
| where the capture is armed from | a page over the link's `capture` command, allowed by a press on the unit (`products/common/services/config_capture.cpp`, `order_capture` in `products/common/services/capture.cpp`) |
| draining this ring to flash | `products/common/services/capture.cpp`, over the store both logs share |

The armed flag lives in RAM only, so a boot always comes up disarmed. A device that kept recording after a power cycle would fill its partition with a capture nobody asked for.

## The record

24 bytes, the geometry of `flight::LogRecord`, so the sector ring, the chunking and the resumable offload are the flights store's and are not written twice.

| Offset | Bytes | Field |
|---|---|---|
| 0 | 1 | type |
| 1 | 1 | flags |
| 2 | 2 | into_ms, milliseconds into the second (`core/events/stamp.h`) |
| 4 | 4 | at_s |
| 8 | 16 | payload |

Every field is written and read one at a time, little-endian, as `core/flight/log_record.cpp` does, never by `memcpy` of a struct whose padding nobody can see.

- `at_s` is UTC once the receiver has given a second, and time since boot before that, as `radio::Entry::at_s` is. `kFlagUtcDated` says which.
- `kFlagPhaseValid` says `into_ms` was measured against a PPS edge. Without it the millisecond field means nothing.
- Those two bits mean the same on every type. Bits 2 to 7 belong to the type and are named in `layout.h`.
- A type is retired, never renumbered, because a corpus outlives the firmware that wrote it and a decoder has only the number.
- Type 0 is not a record, so an all-zero slot decodes as nothing. A slot no writer has reached reads erased (`store::erased`).

### A stored code is a wire format

A payload that carries a C++ enum stores one byte of it, so every such enum writes its numbers down (`BadCrc = 6`) and a new member takes the next free number. Inserting a member in the middle of an unnumbered list would silently change what every stored record means. Some of those enums belong to other features (`core/radio/log.h`, `core/timing/`, `core/power/`, the product's `Page` and `Gesture`), and this layer may not move one of their values.

- The numbers fill 0 up to one less than the count, with no holes, because the host decoder reads a byte through a tuple it indexes by.
- The layout lists every enum a payload stores, with its codes. It declares this layer's own (`Profile`, `Refusal`, `LinkAction`, `SwitchKind`, `LoopService`) in `layout.h`, and mirrors another feature's member by member: the generated `test/core/diagnostics/test_diagnostics_mirrors.cpp` maps each through a `switch` with no default and asserts each code, so a new member fails the build until the layout lists it. `test/products/skyblip_go/test_diagnostics_product_codes.cpp` pins the codes the product owns.
- `scripts/check_diagnostics_layout.py` holds each enum's names in `schemas/diagnostics_log.v1.schema.json` to the layout's. So a new member is an edit to the layout and to the schema in the same commit.
- `schemas/diagnostics_log.v1.vectors.json` holds one record of each type as the firmware writes it. `test/core/diagnostics/test_diagnostics_vectors.cpp` pins the encoder to its bytes and `scripts/test_blip.py` pins the decoder to its fields, so a layout that moves on one side fails a test. A new type or field is an edit to the vectors too.
- `Contact::gesture` is the product's `go::Gesture`, and `Screen`'s page, mode and prompt are `go::Page`, `go::Mode` and `common::Pending`. Each stores that enum's own number, and the schema lists it.

The device never reads a record back. The C++ decoders the host suite reads records with are in `test/support/diagnostics_read.h`.

## What each type answers

`payload.h` lists each type's fields, every one read off `bus::State` or an `events::` value. Nothing here is a sensor this device does not have.

| Type | The tuning question it answers |
|---|---|
| `Boot` 1 | which build and which parts produced the rest of the corpus, whether the device came up from a fault, and which capture the session is |
| `Config` 2 | what the firmware was assuming while it decided everything else |
| `Gnss` 3 | how long after its own quarter second a solution lands (`kFixMaxAgeMs`, 500 ms of §G.1.16 nav age), and what the rejects cost |
| `Pps` 4 | whether `kPpsHoldoverMs` is the right patience, and what the slot map is anchored to |
| `Burst` 5 | every verdict `radio::Verdict` declares against the second it happened in: two devices on one bench read one link twice |
| `Dwell` 6 | whether the dwell map spends the second where §C.5 says, and what refuses a burst |
| `Flight` 7 | how close the machine came to deciding the other way. Written on every evaluation, because a transition-only tape cannot say what the margin was |
| `Power` 8 | what a burst does to the rail, and whether `kLowMv`, `kCriticalMv` and `kFlatMv` fire where a real pack needs them to |
| `Barometer` 9 | the vertical speed window (`kBarometerClimbWindowMs` against `kGnssClimbWindowMs`) measured rather than assumed |
| `Motion` 10 | what the airframe pulled, and whether the filter lengths suit it. Absent on a unit with no hub |
| `Contact` 11 | `Controls::kLongTouchHoldMs` and `kLongPressHoldMs` against the presses a hand makes |
| `Link` 12 | what a phone negotiates in the field, whether frames need splitting (`../comms/README.md`, Measuring what links settle on), and where an offload stalls |
| `Traffic` 13 | `kAdvisoryDistanceM` and `kAdvisoryAltitudeM` against the encounters that happened, and what the ageing rules threw away |
| `Write` 14 | whether `kSettleMs` and `kDeferCeilingMs` place a durable write inside its window, and how often one is forced |
| `Screen` 15 | `ScreenService::kPresentFloorMs` and the refresh cadence against what a pilot was shown, and what a page costs the loop |
| `Gap` 16 | nothing. It is the hole itself, written where the hole is |
| `End` 17 | nothing. It says the session stopped rather than was stopped |
| `Duty` 18 | what each consumer cost over a flight, on a board that cannot measure current |
| `Switch` 19 | whether `kSwitchRetuneMs` can shrink, from every change rather than the worst since boot |
| `Loop` 20 | whether the loop is late because one pass is long or because nothing ran it, and which service holds it |
| `Adsb` 21 | the ADS-B altitude questions of #183: which datum each installation's difference is on, how far the two relative altitudes disagree, and which versions and GVA values are heard |
| `Receiver` 22 | when Bravo hears nothing, whether the LR2021 left receive, reset itself, lost its threshold, stopped raising its IRQ, or sat in noise |
| `Reception` 23 | where the frames the LR2021 heard went: refused, folded, placed, and whether Bravo had a position to place them against |

### Bursts that carry a name or a status

`kBurstFlagCallsign`, bit 6 of a `Burst`'s flags, says own-ship's burst carried a callsign (`radio::Entry::callsign`), so a corpus tells the ident from the positions around it (`../timing/README.md`). `kBurstFlagStatus`, bit 7, marks the Status burst (§G.2). The name itself is not recorded: nine characters do not fit, and `Config` already holds own-ship's address. A name heard needs no flag, because its verdict is `Named`.

### A target whose altitude is unknown

A target can have no relative altitude: it sent none, or it sent a pressure altitude and own-ship has none. `Traffic` writes 0 in `relative_altitude_m` for it, and flag bit 7, `kTrafficFlagRelativeAltitudeValid`, says whether those two bytes are a separation. Older records left bit 7 clear on every target, so bit 6, `kTrafficFlagAltitudeRecorded`, is set on every `Traffic` this firmware writes and vouches for bit 7. A reader takes bit 7 only when bit 6 is set. The flight log's targets carry the same pair (`../flight/README.md`).

### The ADS-B record

`Adsb` is one ADS-B aircraft as an analysis of #183 needs it, and nothing the `Traffic` record already holds:

| Payload bytes | Field |
|---|---|
| 0-2 | ICAO address |
| 3 | version in bits 0-2, GVA in bits 3-4, the widths DF17's type code 31 gives them |
| 4-7 | pressure altitude, feet, as the target sent it |
| 8-9 | GNSS-baro difference, feet |
| 10-11 | the difference's age, milliseconds before the record's instant, saturated |
| 12-13 | relative altitude on GNSS height against own HAE, metres, saturated |
| 14-15 | relative altitude on pressure altitude against own pressure altitude, metres, saturated |

- Flag bits 2 to 7 say which of the version, the GVA, the pressure altitude, the difference and the two relative altitudes are valid. An unknown version is never read as version 0 (`../model/README.md`). The host decoder leaves out every value its flag calls invalid.
- The two relative altitudes are `flight::gnss_relative_altitude_m` and `flight::pressure_relative_altitude_m`. The alarm keeps the first that answers (`../traffic/README.md`), and this record keeps both, because #183 asks how far they disagree. Neither is carried to a common instant.
- There is no position: a `Traffic` record under the same address holds the distance and bearing.
- It is written before the traffic table, because a full table keeps only its nearest targets (`../traffic/README.md`, No gate) and #183 asks about every aircraft heard.
- A paired Go's observations carry no difference, because the `peer` frame does not (#177).

`AdsbCapture` admits a record under two rules, and only a record the recorder took counts against either:

- **Each aircraft at most once each `kRecordPeriodMs`,** 10 s, the span a GNSS-baro difference is trusted (`kHeightDifferenceMaxAgeMs`). An aircraft heard sooner writes nothing and counts nothing.
- **At most `kBudgetCeilingRecords` in any `kBudgetWindowMs`:** ten in any sliding second, which spreads `adsb::Assembly::kMaxAircraft`, 100, over the period. An aircraft due when the second's ten are spent is counted in `refused()` and admitted on a later fold. So a capture armed under a full sky never floods the 64-slot ring.

Ten a second is at most a hundred a period, the size of the table that remembers each aircraft's last record, so an entry is always free or expired when the budget admits a record. A `static_assert` holds the three constants to that arithmetic. A held entry is never recycled, which would record its aircraft twice in one period.

### The receiver and reception records

Bravo writes one `Receiver` and one `Reception` each second of a `Full` capture, so an outage reads as seconds whose counters stop, not as a hole. Every counter crosses as its low 8 or 16 bits since boot, and a reader subtracts two records as it does `Duty`'s. `scripts/reception_check.py` does that and names the outages.

`Receiver` carries the driver's counters and the probe the receiver thread takes once a second (`../../hardware/parts/lr2021/README.md`, The probe). Flag bit 4, `probed`, vouches for the probe's fields: the LR2021's own packet count, its errors, the noise floor, the threshold it holds, its chip mode and its reset source. Read together they separate the causes of a silent receiver:

| What the records show | What it means |
|---|---|
| `part_reset` `power_on`, with `front_end_kept` false and `threshold_dbm` not -110 | the part reset itself, from a brown-out or its rail, and lost its 1090 MHz configuration. `resets` rises a second later, when the driver brings it up again |
| `part_mode` anything but `receive` while `listening` | the part left receive and the driver did not notice |
| `part_packets` rising while `frames` does not | the part decodes frames that never reach the driver: a lost IRQ edge or a FIFO read |
| `missed_edges` rising | frames found on a timed wake, with no IRQ edge to wake the thread |
| `noise_dbm` well above its usual floor | the band is jammed or the front end desensed |
| `frames` rising, `crc` rising as fast | frames heard and refused, as #277 measured on the bench |
| `rearms` or `resets` rising | the driver's own recovery ran, after 30 s of silence or a fault |

`Reception` carries `AdsbService`'s counters: `crc`, `unsupported`, `folded`, `positions` and `capture_refused`, then the aircraft the assembly holds, those the table holds, `reference`, the position the traffic is placed against, and `fix_valid`, flag bit 2. Bravo's NMEA sends no traffic without a fix, and its peer link exports none while `reference` is `none`, so an EFB can lose every target while the receiver hears them all.

### The transmitter

`Config` carries what the sender asked the radio for, so two units' `Burst` records of one burst measure a link (`../radio/README.md`). `tx_power_dbm` is the `SetTxParams` argument and `pa_rated_dbm` the row of SX1262 DS table 13-21 the PA configuration is, both read off `ports::Rf::transmitter()` into payload bytes 12 and 13. There are two numbers because the datasheet reaches +14 dBm two ways. A device with no radio writes zero for both. `scripts/link_budget.py` is the reader.

### The battery

`Power` carries what `core/power`'s ladder decided, not what a decoder can re-derive (`../power/README.md`).

- `power_level` is a decision, with hysteresis and a cable reset, so no comparison on `cell_mv` can replay it. Its codes are not in ladder order: `critical` and `flat` are 2 and 3, and `low` is 4.
- Bit 6, once `caution`, and bit 7 with payload bytes 13 and 14, once the learned trim, stay clear and are never reused.
- `cell_mv` is the gauge's median of three, the figure `percent` and `power_level` were decided on and the one `scripts/power_budget.py` measures a run between. The median hides a burst's sag, so `sample_offset_mv` carries the last reading minus the median, in whole millivolts, saturated at -128.
- `battery_valid` says whether `cell_mv` is the cell. On the cable the divider reads the USB rail, and a reading outside `power::plausible_mv`, 1800 to 4700 mV, is refused and counted in `implausible`. Either way `percent` is 0 and `cell_mv` keeps what the divider read. 0 is an ADC that gave nothing. Builds before 2026-10-05 recorded 0 whenever `battery_valid` was clear.
- `charging` means external power: nothing on this board says when a charge ends.

The lamp condition, the panel's `battery_low`, `may_write` and `may_refresh` are left out, because a decoder computes each from fields already in the corpus.

### Duty, a power budget with no shunt

There is no current sense on this hardware. `Power` writes the terminal voltage once a second, and `Duty` how long each consumer was switched on. A reader takes two `Duty` records, subtracts each field, divides by the seconds between them, and multiplies by the consumer's bench current. The error is the bench measurement's.

All seven fields are `uint16_t` from `uint32_t` counters, so the codec keeps the low half. Readers subtract, and unsigned subtraction crosses one wrap correctly, where a clamp would stick at 65535. That holds while one interval stays under 65.536 s, so `kDutyPeriodCeilingMs`, 60 s, bounds the cadence and the emitting service asserts its own period against it.

**A reader must not subtract two `Duty` records across a `Gap`, a `Boot`, a session boundary, a missing or unreadable record index, two different datings (UTC and since boot), or a pair further apart than `kDutyPeriodCeilingMs`.** Any of those may hide a wrap or a counter reset. The interval is dropped, never estimated.

### The last pair, when the device goes down

A capture that parks ends on one more `Power` and one more `Duty`, off the cadence, then its `End`. `Product` asks `PowerService::record_last_pass()` for them on the way down, after the radio sleeps and before `CaptureService::park()` disarms the recorder, whatever took the device down. Without the pair a power run would rarely record its cutoff, because the loop stops on the pass a shutdown is asked for.

A run to cutoff closes on `Power` with `power_level` `flat`, then `Duty`, then `End`, under one instant. A ring rotation while writing the pair puts its `Gap` after that record. A partition that refuses the pair's slots ends the session on `Gap` then `End`. So a reader looks for the last `Power` and `Duty` before the `End`, not for the two slots in front of it. A pilot's stop is not a park and writes no pair.

### The end marker, and the tail a reader must drop

A 24-byte slot has no CRC, so a power cut in the middle of a program leaves a slot that decodes. The session says it instead. A capture that stops cleanly, by a disarm, a power off or a full partition, writes an `End` as its last record, and the store reports `closed: true` only for a session whose last record is one. **The last record of an unclosed session may be torn, and a reader drops it** (`scripts/blip_ndjson.py` does).

The session the unit is still writing has no `End` yet, so its entry says `recording` beside `closed: false` (`../comms/README.md`, The log dialect). Its last record is whole, because a record is written whole inside one loop pass, so a reader keeps it.

The writer keeps the last two slots of its frontier sector for a `Gap` and an `End`.

## Where each type is produced

One tap per fact, in the service that owns the field on `bus::State` (`core/bus/README.md`), guarded by `armed()` before a payload is built.

| Type | Tap | When |
|---|---|---|
| `Boot` | `products/common/services/capture.cpp` `record_boot` | as a capture session opens |
| `Config` | `products/common/services/capture.cpp` `record_config`, with the settings `products/skyblip_go/capture_settings.h` describes | the same instant |
| `Gnss` | `products/common/services/ownship.cpp` `record_gnss` | the solution on each whole second the receiver publishes, fix or not, so one in two at 2 Hz |
| `Pps` | `products/common/services/ownship.cpp` `record_pps` | every latched edge, and every `kPpsRecordPeriodMs` that brings none |
| `Burst` | `products/skyblip_go/services/traffic.cpp` `log`, `products/skyblip_go/services/radio.cpp` `log_refusal` | every tape entry: what the air delivered, and what policy refused to key |
| `Dwell` | `products/skyblip_go/services/radio.cpp` `record_dwell` | every dwell armed, three a second |
| `Flight` | `products/common/services/ownship.cpp` `record_flight` | every evaluation of the flight machine, on a whole-second solution |
| `Power` | `products/common/services/power.cpp` `record_power` | `PowerService::kRecordPeriodMs`, and once more as a capture parks |
| `Barometer` | `products/common/services/ownship.cpp` `record_barometer` | one sample a second of those the board publishes (`kBarometerRecordFloorMs`) |
| `Motion` | `products/common/services/ownship.cpp` `record_motion` | `OwnshipService::kMotionRecordPeriodMs`, where a hub is fitted |
| `Contact` | `products/skyblip_go/services/screen.cpp` `record_contact` | every debounced edge, with the gesture the product made of it |
| `Link` | `products/common/services/config_link.cpp` `record_link` and `record_link_down`, `products/common/services/flight_log.cpp` `record_link` | a connection up or down, the claim changing hands, a frame in, a frame dropped |
| `Traffic` | `products/common/alarm_grader.cpp` `record_traffic` | every reception that reached a target, once graded |
| `Adsb` | `products/common/services/adsb_capture.cpp` `AdsbCapture::record` | every ADS-B observation folded, under the two rules above, from Bravo's `AdsbService` and a paired Go's `BravoService` |
| `Receiver` | `products/skyblip_bravo/services/adsb.cpp` `record_second`, from `ports::AdsbReceiver::statistics()` | once a second, whatever the sky does |
| `Reception` | the same | the same pass |
| `Write` | `products/common/services/config_link.cpp` `record_write` | every verdict of the durable-write window that is not `Idle` |
| `Screen` | `products/skyblip_go/services/screen.cpp` `record_screen` | `ScreenService::kRecordPeriodMs`, the render cadence |
| `Gap` | `recorder.cpp` `flush_gap`, `products/common/services/capture.cpp` `write_gap`, `announce_rotation` | where the ring refused a record, the partition refused a sector, or the sector ring recycled one of this session's own |
| `End` | `products/common/services/capture.cpp` `write_end` | the last record of a session that stopped rather than died |
| `Duty` | `products/common/services/power.cpp` `record_duty` | on the `Power` pass: `PowerService::kDutyRecordPeriodMs` in a full capture, `diagnostics::kPowerRunRecordPeriodMs` in a power run or a flight run, and once more as a capture parks |

- `Gnss::reject` is the receiver's verdict. No `runtime::Context` reaches the L76K driver's `gnss::FixValidity`, so the board publishes its reason and count onto `bus::State` beside the sky view.
- `LinkAction::Sent` is never written. No place that sends a frame can see the claim the record carries, and a `Sent` from the log endpoint alone would read as a device that answered nothing on config. The action keeps its number.
- `Gap::span_ms` is zero on the marker a full partition leaves, because the ring does not hand out its queued records' instants.
- `Write::kind` is always `Settings`. The flash rings ask `timing::DurableWriteWindow` whether a phase is free (`RecordPool::book_window()`, `products/skyblip_go/flash_window.h`), so they leave no verdict to record.

## The recorder

`record()` returns on the armed flag before it encodes anything, so a disarmed device pays one branch per tap. A tap that gathers its fields first checks `armed()` itself.

### The three profiles

A profile is what a capture is for, chosen when it is armed and held in RAM beside the armed flag.

- `Full` lists every type: the corpus the tuning work replays. A capture may take 170 sectors of the Go's 298, because the flights floor keeps 128 (`../store/README.md`), so 28,900 records. At `Recorder::kPeriodicRecordsPerSecond`, fifteen a second, that is about 32 minutes, and about 19 minutes with a full ADS-B sky's ten more a second. Bravo's X1 has 512 sectors, so a capture takes 384 of them, 65,280 records. With no traffic Bravo writes about eight a second, `Receiver` and `Reception` included, which is about 2 h 15 min. Each position heard adds a `Traffic` record, so ten aircraft sending two positions a second leave about 37 minutes. The ring recycles the capture's oldest sector, so a capture keeps the latest stretch of that length.
- `PowerRun` lists `Boot`, `Config`, `Power`, `Duty`, `Gap` and `End`. A run to cutoff on the 2400 mAh pack is 35 to 50 hours. Two records every 30 s holds about 120 hours, so the run fits whole, start included. The cadence is one constant, not a knob, and `Power` and `Duty` go out on the same pass so a reader never interpolates between two clocks.
- `FlightRun` lists the same six at the same cadence (`paced()`), and is the one profile that changes what the device does (`simulates_flight()`): the glass runs as in flight, with the radar's flight clock, the `FLIGHT` word and the g-meter's extremes cleared at the simulated takeoff (`../../products/skyblip_go/README.md`). Everything else reads the real flight state. A parked unit keeps the ground transmit cadence and on-ground flag, because ADS-L G.1.16 holds an on-ground report to 0.1 Hz and a false airborne aircraft would mislead every receiver in range. The flight log writes no flight, so the run leaves no fake flight and evicts no real one. `scripts/power_budget.py` models the transmit the run could not send.

The profile is payload byte 14 of `Boot`, with flag bit 2 set to say the byte was written. Older `Boot` records left both zero, and zero is `Full`, so a reader takes the byte only when the flag is set.

A type the profile does not list is refused and **not counted as a drop**, because a `Gap` naming records the profile never wanted would lie about a hole. `Boot`, `Config` and `End` are in all three profiles, because a corpus that cannot name its build and settings, or say whether it ended or died, is not evidence.

### Peek, commit, and a full ring

The writer peeks and commits rather than taking. A flash that refused a write still owes that record, so it stays at the head and the next pass tries again. Only a record the flash has taken leaves the ring. `flight::LogSession` drains the same way.

A full ring refuses the new record and keeps the history, because a corpus is read forward from a known start. The refusals are counted, the first and last remembered, and as soon as a slot frees the ring writes a `Gap` stamped at the instant the hole opened: how many were lost and over what span. That marker costs a slot, so a ring that stays full keeps dropping.

### Why 64

The ring covers the worst window a writer can be blocked for, at the rate a busy second produces records. The window is about a second: a 4 KB sector erase as the frontier moves, and a writer that stands off the direct slot (450 to 1000 ms, §C.5). The rate is the fifteen periodic records plus the air, and thirty a second is a generous reading of that. 64 slots is two such windows, 1536 bytes of RAM. ADS-B adds at most ten records in any second, so fifteen and ten leave five of the thirty for the air. A sky that produces more records its gaps as gaps.

`kPeriodicRecordsPerSecond`, fifteen, counts the taps that write whatever the sky does: eight subjects once a second (`Gnss`, `Pps`, `Flight`, `Barometer`, `Power`, `Motion`, `Screen`, `Loop`), three dwells, the three `Switch` records they cost, and own-ship's `Burst`. It is the quiet-second rate the capture page quotes before a capture is armed. Once one runs, the page quotes the measured rate. `test/products/skyblip_go/test_diagnostics_taps.cpp` holds an armed device to that number.

## Where the recorder lives

On `runtime::Context`, beside the roles, the bus and the blackboard. It is many-producer and single-consumer, where `core/bus`'s queues take one producer each and the blackboard one writer per field. So a tap is one line with no wiring, and `Context::instant()` gives the stamp.

The pool rotates: a long capture keeps its most recent hours, and the sector ring may recycle a sector of the session being written (`../store/README.md`). The writer then writes a `Gap` naming how many records went with the sector, and writes `Boot` and `Config` again behind it, so a corpus read from the middle still names its build and settings. The `list` reply calls that session truncated.

## The layout and the schema

[`schemas/diagnostics_layout.v1.json`](../../../schemas/diagnostics_layout.v1.json) is the one place a record is laid out: each type's code, and each field's offset or flag bit, its kind and its enum. `scripts/diagnostics_layout.py` generates `layout.h`, the mirrors test and the decoder tables of `scripts/blip_records.py` and the console's `diagnostics.js` from it, and `scripts/check_diagnostics_layout.py` fails when one of them is stale. To add a type or a field, add it to the layout and run the generator, write it in `payload.h`, its `record_of()` and `test/support/diagnostics_read.h`, describe it in the schema, and add it to the vectors. A field the firmware stops writing is marked `retired`, so the decoders still read older captures and no new field takes its bytes or bit.

[`schemas/diagnostics_log.v1.schema.json`](../../../schemas/diagnostics_log.v1.schema.json) is the decoded view: one decoded record per type, with what each field means. It stays written by hand, because one property serves every type that has the field and its prose and ranges are no part of the layout. It is not the `diagnostics` frame of `schemas/link.v1.json`, the live status dump the companion link answers with (`core/comms/diagnostics.h`).
