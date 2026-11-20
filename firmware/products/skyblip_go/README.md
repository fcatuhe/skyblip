# products/skyblip_go

`product.h` wires one board, one service list and the pages. The shell around it, `main.cpp` on silicon or the simulator on the host, only decides how often `run_pass()` runs and where the pixels go. `pages/` is the glass, `input/` is what a press means, and `glass.h` names `go::Glass`, sized by the board.

`settings.h` lists what a pilot can change, the defaults and what it refuses. A setting with no reader leaves the struct, the link registry and the schema together. `kBlobVersion` moves whenever the struct's bytes move, and `from_blob` reads that version alone, so an older layout reads as defaults, since only dev units stored one. The settings live in the product, not on core's `bus::State`, and the product hands a reference to the services that read them.

`settings_store.h` is the Go's `common::SettingsStore` (`../common/README.md`), the only coupling between the companion link and the struct. It reports `address` once `state.activation` has a serial and `address_mapping_table` always, and nothing can set either. It refuses a `set` of `aircraft_address` against `state.own_transponder` (Aircraft address).

A layout cannot be read backwards, yet the image the bootloader reverts to meets a blob its successor wrote. `ConfigLinkService::load()` tells a newer layout from a torn sector by the CRC over the stored length (`settings::sealed`) and leaves the newer one untouched, using `settings.prior` instead, which `install()` fills on the way into a swap. So a revert comes up on the pilot's settings, and a reinstall finds its blob. A fallback is said as `"settings":"prior"` or `"defaults"` on the `update` frame, and as `DEFAULTS` on the self test's `STORAGE` row.

## The GNSS rate

The Go asks its L76K for two solutions a second, `kSolutionRate` in `product.h`. ADS-L dates a position to a quarter second (G.1.1) and keeps it under 500 ms old on air (G.1.16), so the solutions fall on `.000` and `.500`, and a position drawn after 490 ms waits for the `.500` one (`../../core/timing/README.md`, The age of a position). Once a second would hold most airborne bursts. `common::solution_rate_serves` refuses at compile time an ADS-L product whose receiver gives fewer. The flight model, the flight log and the EFB read whole seconds, so nothing else depends on the rate.

## Aircraft address

`aircraft_address` is the 24-bit ICAO address of the aircraft's own Mode-S transponder, 0 while unset. While set, the Go transmits under it, table 5, instead of table 58 and its serial, on every payload, as ADS-L F.2.3 asks (`../../core/settings/README.md`, The ICAO address), and `status` names it as the unit's address. `TrafficService` hands it to the traffic table every pass, which refuses it, so the aircraft's own transponder is never traffic (`../../core/traffic/README.md`, Our own aircraft). A pilot sets it from a paired Bravo's proposal with one press (`pages/README.md`, aircraft), on the `ADS-B` row, over the link (`aircraft_address` in `schemas/link.v1.json`) or on skyblip.eu. 0 over the link clears it, after the glass asks, as for every `set`.

F.2.3 also asks to refuse an inconsistent configuration. `validate()` refuses 0xFFFFFF, which ICAO assigns to no aircraft, and `contradicts()` an address other than the transponder a paired Bravo names. `BravoService` publishes that transponder as `bus::State::own_transponder` while Bravo is live and on the ground, and `status` carries it to a Briefing page (`../../core/comms/README.md`). A `set` is refused `not_own_transponder`, at the prompt and again at the press. A clear is never refused, because F.2.3 asks for a revertible setting. The rules left out are in `../../core/settings/README.md`.

`aircraft_address_warning.h` flags a paired Go and Bravo that both lack an address, since the Go may then alarm on its own transponder (`../../core/traffic/README.md`, Accepted for v1). Only the self-test page shows it, as `ADS-B` and `UNSET`, because most transponders sit in `STBY` on the ground and a notice every boot gave nothing to act on.

`BravoService::own_transponder()` keeps the last transponder a fresh `health` named (`../skyblip_bravo/README.md`, Own transponder), and `ScreenService::detected()` turns it into the `AIRCRAFT` block, with the callsign's dash restored by `adsb::registration`. `go::adopt` is the one write behind the menu and the `Notice::NewAircraft` notice, and each stores only what it drew while Bravo still hears it (`ScreenService::detected_to_store`, `ScreenService::notice_read`). The next `peer_claim` carries the new address, and Bravo stores it too.

## Bravo

`services/bravo.h` is the Go's link to a paired skyBlip Bravo: the pairing, the pin in the settings and the connection (`../../core/comms/README.md`, "The Go's link to its Bravo"). `services/bravo_fold.cpp` folds Bravo's observations into the traffic table, and `bus::State::bravo` publishes Bravo's health (`../../core/comms/README.md`, "What the Go makes of its Bravo"). Its claim carries the `ALARM` row and the Go's call, so Bravo plays the Go's calls, and it is handed the alarm service because a press on Bravo dismisses the Go's alarm (`../../core/comms/README.md`, The alarm).

## What a service is

A service is one unit of work the loop steps, under the watchdog, in dataflow order. It owns one thing, a queue it drains, a slice of `bus::State` it writes or an output port it drives, and never a queue another service drains. `nmea` is a service though it drains and writes nothing, because it drives `ports::Link` on a cadence that defers around an armed burst. Code that reads the finished product without being stepped, such as `DiagnosticsCollector`, is a reader.

`OwnshipService`, `PowerService`, `AlarmService`, `NmeaService`, `FlightLogService`, `CaptureService` and `ConfigLinkService` are shared, in `../common/services/` ([`../common/README.md`](../common/README.md)). `TrafficService`, `RadioService`, `ScreenService` and `BravoService` are the Go's own, in `services/`. The self-test page, its parts table and the glass on the way down stay in `product.h`, and the rest of the way down is `common::Shell` (`../common/shell.h`).

The order in `services_` is the dataflow, never a preference. `ownship` publishes the fix before `traffic` measures against it, and `traffic` stamps the transmit before `radio` closes its deadline against it. `flight_log`, `capture` and `screen` come late, because each may spend a whole pass on flash or pixels, and `bravo` comes last. The loop meter names each service by its `diagnostics::LoopService` code (`core/diagnostics/payload.h`), the same in either product.

`due_in_ms()` says how long the loop may sleep before a service needs a pass, one pass by default, `runtime::kServicePassCeilingMs`. The loop sleeps until the earliest deadline, at most `runtime::kServiceRestCeilingMs`, unless a producer wakes it.

Everything a service needs arrives at construction, through the `runtime::Context`, which holds the roles, the bus, the state and the diagnostics recorder, or as a reference. There is no `attach_` and no member that starts null, because a half-built service makes every method check. The recorder sits in the Context because it has many producers, which neither a queue nor a one-writer field allows (`../../core/diagnostics/README.md`).

`AlarmService` alone drives what this product says aloud or shows with a light: nothing else calls `tone()`, `silence()`, `vibrate()`, `still()` or `show()`, so each arbitration rule is written once, in `core/annunciation` and `core/indication`. On the `ALARM` row, `OFF` takes neither the glass nor the lamp, `VISUAL` takes them silently, and `SOFT` and `LOUD` set the tone's `ports::Loudness`, with the motor running each call. A Go with neither buzzer nor motor plays the `BRAVO` steps as `VISUAL`, and its Bravo sounds them.

## The partition, and the two logs on it

The pool, its two rings and the two log services are shared ([`../common/README.md`](../common/README.md)). The Go adds its flash rule, the glass that arms a capture, and the way down.

### When flash may be touched

The Go hands `RecordPool` the rule `book_flash_in_dwell()` from `flash_window.h`, which asks `timing::DurableWriteWindow::free_now()`, through `bus::RfState::book_flash_window()`, against the dwell phase the radio publishes: inside a running dwell, done a guard's width before it must be re-armed, and never where own ship may key the PA. A pass that spends 200 ms on flash outside that window misses the next dwell edge.

The window books `kSectorEraseCostMs` per sector erase and `kSlotWriteCostMs` per page program (`core/store/sector.h`), budgets for the external NOR that the bench must still settle, and a `static_assert` in `flash_window.h` fits a claim into the narrowest dwell. Work is booked per pass, after what the pass already booked, across both rings and the board's copy of the hub's image (`hardware/parts/bhi260/firmware/README.md`), because the published phase does not advance inside a pass. The uplink dwell is the one long stretch the second offers (`core/timing/README.md`), so erasing the whole partition takes about 40 seconds, with progress reported throughout.

### Which capture is armed

A capture is `Full`, `PowerRun` or `FlightRun` (`../../core/diagnostics/README.md`). A page arms one over the link's `capture` command, as on Bravo (#247), so a pilot can measure a unit in the field. The confirm page names the profile and the flights it erases, `START A POWER RUN, ERASES 3 FLIGHTS` (`../../core/comms/README.md`, The capture group), and one press allows it, through `take_confirmed()` and `common::order_capture()`. The lamp winks twice every 3 s while a capture runs (`../../core/indication/README.md`). The running profile lives in `diagnostics::Recorder`, so a boot comes up armed with nothing.

A flight run flies the glass and nothing else, in `OwnshipService`: the `bus::FlightState` clock comes from a second `flight::FlightTimer` held airborne, and the g-meter clears its extremes on that takeoff. Those fields are read only by the glass and the g-meter. Everything else reads `state.own.flight_state` or the ground latch, which stay real, so the transmit, the flight log and the offload and update gates are unchanged.

`CaptureService::keeps_s(profile)` is the span a capture keeps before it is armed, the slots it buys over `diagnostics::Recorder::records_per_hour(profile)`. `CaptureService::describe()` reports it, because `bus::CaptureState` holds only the running capture's span.

### How a capture ends

The three ways a capture ends are in [`../common/README.md`](../common/README.md). At power off, the shutdown sequencer parks the capture after `rf.abort()` and `rf.sleep()`, and `Product::publish_radio_asleep()` clears the plan and phase that still promised a burst, so the drain does not wait past the rails. The park drains to exhaustion and then closes, because a session closed over records in hand loses its last second. `PowerService::record_last_pass()` queues one last `Power` and `Duty` first, so a session ends on that pair and its `End` (`../../core/diagnostics/README.md`).

## Capabilities

A capability is what the board has, probed at bring-up and granted from the devicetree (`ports/capabilities.h`). A service reads `context.roles.capabilities` in its `setup()`, so a missing part is skipped rather than broken.

## The parts of a boot

`setup()` probes the board, classifies the reset, and first decides whether this boot becomes a device. The factory bootloader sends a charger's VBUS wake straight back to SYSTEM OFF (`../../boards/lilygo/t_echo_plus/factory/README.md`), and `core/power/wake.h` refuses it again with no service started.

A refused boot names a flat cell too. `park_refusal()` settles the frame `core/power`'s `refused_frame` owes the glass, waits for the button to stay up `kReleaseSettleMs`, because the wake pin senses a level, and drops the rails with the button armed. `kRefusalParkCeilingMs` bounds the frame, so a panel that never reports ready cannot hold a flat cell awake. The refusal comes after `board_.begin()`, because it needs the reset cause, the trim from NVS and a cell reading, so a refused press costs a whole bring-up, unmeasured. Moving the IMU blob check after the refusal is the one cheap saving.

The board's constructor touches no part, because the product is a global built before `main()` arms the watchdog. `setup()` opens with `Board::probe()`: the SX1262's register round-trip, the DRV2605's identification, the BME280's chip id, and the panel ID and buzzer pin read at `PRE_KERNEL_1` by `boards/lilygo/t_echo_plus/board.c`, before the SPIM and the PWM take those pins. The round-trip runs before `begin()` waits for the TCXO, since DS 13.1.1 starts the part in STDBY_RC, whose internal clock serves it. The radio and annunciator roles, `t_echo_plus::ProbedRf` and `ProbedAnnunciator`, act as null parts until `probe()` proves the part, and `setup()` names the unit from the probed capabilities.

The watchdog is armed before `setup()`, so a part that hangs bring-up bites and resets rather than leaving a unit awake. `park_refusal()`'s loop feeds it, and SYSTEM OFF stops it. The bite returns as `WATCHDOG`, a restart the boot path holds only to the cutoff, so a hang that repeats shows in the `diagnostics` dump. Bring-up must fit the software channel, `CONFIG_TASK_WDT_MIN_TIMEOUT` less a second (`hardware/platform/zephyr/watchdog.h`).

A failed self test leaves a device that shows why: the page stays, the button works, and the loop refuses to fly while `flyable()` is false, a part of `kRequired`, the radio, the receiver or the barometer, missing. Such a unit cannot be activated and advertises nothing, since no service would answer. A flying unit advertises only after `config_link_.load_identity()` verifies the unlock code, so its first name says whether it is activated.

## Go and Go+

A unit is a skyBlip Go+ when `Inclinometer`, `Buzzer` and `Haptic` are all fitted, answering or not, and a skyBlip Go otherwise (`product_name.h`). Only the Bluetooth name says it, `skyBlip Go+ 00012A` (`core/identity/README.md`). The alarm sounds on a buzzer or a haptic, and a Go with neither sounds only through a paired Bravo (`../../core/comms/README.md`, The alarm).

## Activation

Until skyblip.eu signs it a serial, the unit is silent: `RadioService` arms nothing, the glass shows the activation page instead of every page (`pages/README.md`), and the name is `activate 3F2A skyBlip`. `ConfigLinkService` owns the `identity::Unit` behind the config commands (`core/comms/README.md`) and publishes `state.activation` each pass. An unlock renames the link live through `ports::Link::advertise_as`. A refused name is offered again every `kAdvertiseRetryPeriodMs`, so a dead Bluetooth logs once a second, not every pass. The unlock page is written through `unlock_writes_`, a `timing::DurableWriteWindow`, so the erase lands outside a dwell. The power-off hold still works on the activation page, since the shutdown sequencer reads it off the pin.

## What the glass wears while the device is off

E-paper keeps its last image with the rails down, so the frame pushed just before they drop is what the device wears in a flight bag. `product.h` picks it by why the device is going down.

| Frame | When |
|---|---|
| the wordmark | a long press, or the companion link asking |
| blank | a stow, the long press with the pad held, because months of one image ghost for good |
| installing | an install, so a blank bootloader glass is not taken for a dead unit |
| recovery | a recovery: the unit is a USB drive named TECHOBOOT, a `.uf2` dropped on it replaces the firmware, RESET returns to skyBlip, and nothing transmits meanwhile |
| `BATTERY EMPTY` under the mark | the cutoff, or a refused boot on a flat cell, the one frame that says something happened |
| `CHARGE THE BATTERY` under the mark | a long press or the link switching off a cell at `Low` or `Critical` (`core/power/README.md`), because the lamp dies with the rails |

The recovery frame's line follows `ports::RecoveryPath`. A running watchdog would follow a reboot into the bootloader and cut its USB session, so on silicon the device powers off with the recovery magic set and asks for the press that finishes it. `enter_recovery()` runs only once the frame is on the glass.

After `BATTERY EMPTY` the button stays armed, and a press is refused without touching the glass. The cable charges without waking the unit (`core/power/README.md`), and a press on it gets a device, which reports the flat cell once through `went_dark_flat()`, a retained bit, on the self-test page and the status reply.

No frame carries the percentage, because a figure frozen at zero through a whole charge would be wrong exactly when it matters.
