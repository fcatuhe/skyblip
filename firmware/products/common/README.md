# products/common

The services more than one product runs, and the vocabulary they read. Why they live here and not in `runtime/` is in [`../../README.md`](../../README.md). What a service is, and the order a product steps its services in, is in [`../skyblip_go/README.md`](../skyblip_go/README.md), "What a service is".

`services/` holds these services, in namespace `common`:

- `OwnshipService` owns `state.own`: it drains `bus.gnss`, `bus.barometer` and `bus.accelerometer`, and publishes the fix, the climb, the turn, the flight state and the ground latch.
- `PowerService` owns `state.power`: it drains `bus.battery`, reads the die temperature, and runs `core/power`'s cutoff rule.
- `AlarmService` owns the buzzer, the motor and the lamp. Its `AlarmGrader` (`alarm_grader.h`) grades each target and picks the call this unit plays. It also plays the signs on and off, because the loop no longer runs while the device goes off. It publishes its call as `state.alarm_call`, which a paired Go sends its Bravo, and a paired Bravo plays `state.followed_call` instead of its own grading (`../../core/comms/README.md`, The alarm).
- `NmeaService` is the single writer of `events::Endpoint::Nmea`: the FLARM sentences a phone's map draws, once a second.
- `FlightLogService` writes the flights ring: a takeoff opens a session and a landing closes it. It also drains `bus.log_rx` and answers the Log endpoint for both rings.
- `CaptureService` drains `diagnostics::Recorder` into the diagnostics ring while a capture is armed.
- `AdsbCapture` is not a service. While a capture is armed it writes a `diagnostics::Adsb` record per ADS-B observation, at most once per aircraft each `kRecordPeriodMs` and `kBudgetCeilingRecords` in any second. Bravo's `AdsbService` calls `record()` after `protocol::adsb_fold`, and the Go after each `peer` observation, both before the traffic table (`../../core/diagnostics/README.md`, "The ADS-B record").
- `ConfigLinkService` is the companion link's device side: it answers `config`, `set` and the other config commands through `ConfigService` (`services/config_service.h`), loads and persists the product's settings, names the unit on the air and takes a running image off probation.
- `ConfigService` is not a service. It is the product policy of the config endpoint, owned by `ConfigLinkService`: which commands a product answers, the prompts, the flight gate, the upload window, pairing, captures, activation and trusted browsers. It sends through `comms::ConfigChannel`, which holds the claim and the replies (`../../core/comms/README.md`). `config_service.cpp` holds the dispatch, the settings and the update. `config_prompt.cpp`, `config_reports.cpp`, `config_identity.cpp`, `config_browser.cpp`, `config_pair.cpp` and `config_capture.cpp` each hold one group of commands.

No file here includes `products/<product>/*`. A service gets what it needs from a product at construction.

The Kconfig both products share is here too. `prj.conf` loads before each product's own (`cmake/skyblip.cmake`), `sysbuild/mcuboot.conf` after each product's MCUboot fragment (its `sysbuild.cmake`), and `release/` is what `scripts/build_image.sh` adds to both images of a production build.

The signing key is never in the repository. Kconfig expands `$(VAR)` only inside Kconfig files, so `scripts/build_image.sh` passes the key path in a generated `SB_EXTRA_CONF_FILE`. Without it a local build signs with MCUboot's published key, which proves nothing, and CI fails unless the image verifies against `firmware/keys/`.

## What stays with the product

A shared service reads the parts fitted from `context.roles.capabilities`, so a part the board lacks is skipped, as the inclinometer and the die sensor are. The self-test page and its parts table are the product's, because they name the parts its board carries.

`shell.h` is what both products run the same way: the boot verdicts, the metered pass, the confirmed link actions and the way down, which `core/power/shutdown.h` decides. `common::Shell` parks the radio, the outputs, the settings, the power record and the capture in that order, through hooks the product provides (`park_radio()`, `radio_part()` and the glass hooks Bravo leaves empty). `main_loop.h` is the `main()` loop both images run, and `diagnostics_collector.h` the dump's collector, to which each product adds its radio's group.

`PowerService` reads the cell through `core/power`'s gauge curve, voltage ladder and charge window, which hold the values of the Go's 4.2 V LiPo pouch (`../../core/power/README.md`). Bravo reads its own cell through the same values until #169 measures a discharge curve (`../skyblip_bravo/README.md`, The cell). The seam lands with the measured values, because a seam with one set of values behind it has nothing to choose between.

`PowerService` reads the die temperature every `kDiePeriodMs`, 10 s, because the die moves in minutes. The read sleeps on the HFCLK and a semaphore (`drivers/sensor/nordic/temp/temp_nrf5.c:45-91` at Zephyr v4.5.0), so it runs on the service pass, never from an interrupt or the radio thread.

## Own pressure altitude

`OwnshipService::apply_barometer()` turns each barometer sample into a pressure altitude on the standard atmosphere with `flight::pressure_to_altitude_mm`, and derives the barometric climb from it. It publishes `pressure_altitude_mm`, `pressure_altitude_at_ms` and `pressure_altitude_valid` on `Ownship`, and `tick()` clears the flag once no sample has arrived for `kBarometerMaxAgeMs`. The Go publishes it from its BME280 and Bravo from its SPA06. Its only reader is an ADS-B target that sends a pressure altitude and no GNSS height (`../../core/flight/extrapolate.cpp`).

## The NMEA stream

`NmeaService` sends `$PFLAU` first and outside the per-pass cap, so a full sky never pushes the alarm off the end. Then come `$GPRMC`, `$GPGGA`, `$PGRMZ`, `$LK8EX1` and up to `kTargetsPerPass` `$PFLAA`. The rotation resumes where the last pass stopped, so every target reaches the tablet within `kTargetRefreshCeilingMs`, the alarm's own freshness rule.

A pass goes once a second, as the receiver's whole second lands in `bus::State::own_second`, so `$GPRMC` and `$GPGGA` reach a tablet with no GNSS about 0.1 s after the instant they name, and the `$PFLAA` offsets are measured from that same fix, carried to the pass. A pass on its own 1 s cadence, as before, held the whole second up to a second. An armed burst still holds the pass, at most `kPassDeferralCeilingMs`. Airborne, window B's dwell runs to 202 ms of the next second and carries own-ship's burst every other second, so about every other pass goes up to 0.2 s after its second. A whole second not landed within `kWholeSecondWaitMs` of its due time is lost, and the pass goes without it. That wait counts from the last whole second landed, so a run of lost ones keeps the passes 1 s apart and a full table within its refresh bound. One landing less than `kPassGapFloorMs` after that pass waits for the next second, so the tablet never gets two passes in one second. With no fix the pass keeps its plain 1 s cadence.

- `$PGRMZ` carries pressure altitude on 1013.25 hPa, because XCSoar and the other EFBs read it as barometric and apply their own subscale. A QNH-corrected figure would be corrected twice, and a GNSS height would feed an app's altimeter a geometric one. With no barometer it is not sent.
- `$LK8EX1` goes out without a barometer too, with sentinels in its pressure fields, because it is the only sentence that carries the battery to an EFB.
- A pass is one byte stream cut into frames of the link's size, not one sentence per frame. A `$PFLAA` is wider than the 20 bytes BLE guarantees, and every consumer resynchronises on CRLF. A full-table pass measured 549 bytes, three notifications at 244 bytes.

## Settings

A product owns its settings struct, its blob and its link keys. A shared service reads the fields it needs through a small struct of references the product fills at construction, so a change from the link or the menu reaches it on its next tick. Each struct refuses a temporary, so no reference dangles.

- `AlarmSettings`: the alarm. Bravo hands it three references: its own alarm, its Go's last alarm and whether it holds a pin. `in_force()` is the Go's while paired, link down included, and Bravo's own otherwise.
- `OwnshipSettings`: the aircraft type own-ship stamps on `state.own`.
- `PowerSettings`: the battery trim in millivolts.
- `TrafficSettings` (`traffic_settings.h`): the aircraft address the traffic table refuses, 0 while unset.
- `CaptureSettings` is not references. A `diagnostics::Config` record carries values worked out from settings, such as `metric` from the Go's `Units`, so the product implements `describe()` and the capture calls it at each `Config` record. The Go's is `../skyblip_go/capture_settings.h`.

`settings_store.h` is the seam between the config link and a product's settings. The product owns the struct, its layout and its link keys, and reads a blob of another layout version as defaults. The link owns when the blob is read and written: the durable-write window, the power rule, a newer layout kept for a reverted image, and the copy kept for a revert. A blob wider than `ConfigLinkService::kBlobBytes`, 64 bytes, counts as a failed write. The dump's system group carries the count as `failed_writes`, the one place a change the flash did not keep shows. Only a store with no settings at all is a fresh unit. A read the store fails, which `ports::KeyValueStore` answers `Down` for a flash error, starts the unit on defaults and says so, as an unreadable layout does.

### When the settings are written

`ConfigLinkService` is the only writer of the blob, so it places the NVMC stall where the radio can afford it (`core/timing/durable_write.h`). The glass shows a new value at once, and only the flash waits.

Below the low-battery warning the settings sector is not touched, because NVS garbage-collects in the internal flash the image runs from, and a write may land as a 14 dBm burst sags the cell. A change from a phone is refused at the door with the reason. A change already pending is held dirty, so a charger arriving still saves it. The deliberate power-off flushes a pending change past the write window but never past the power rule, so a `LowBattery` shutdown is the one power-off that loses it.

### What the link takes from the product

- `power::CutoffMonitor`, to ask whether the cell allows a settings write.
- The product as `dfu::Product` and its name, such as `skyBlip Go+`, for the `identity` frame and the advertisement.
- An `identity::NameRule`. The Go's advertises `activate 3F2A skyBlip` until the unit is activated, because a Go must be activated before it transmits. Bravo's advertises `skyBlip Bravo 3F2A` from the FICR tag, because Bravo is never activated.

### Prompts and the press that answers them

A link prompt is confirmed on the device through `ConfigService` (`config_link().config()`): `ScreenService` shows it on the Go's glass and `ButtonService` on Bravo's lamp (`../skyblip_bravo/README.md`). The link never shows or answers a prompt.

Both products answer with `PromptAnswer` (`prompt_answer.h`). One press allows, the Go's pad refuses, and thirty seconds expire a prompt on either. The press is the security boundary: BLE pairing is off, so nothing proves the phone is the pilot's, and physical presence stands in for it. `PromptAnswer` arms only once the prompt is readable and the button has been up for `kAnswerSettleMs` since its last release and since the prompt arrived. A down that began at or before the arm instant disarms it, dated by the board's debounce edge (`events::ContactEvent::at_ms`), so a button already held when a phone asks cannot answer with its release (#176, #245). A prompt replaced by one of the same kind disarms it too, because the product calls `ask()` on every change. Bravo's pairing press arms through the same `PromptAnswer` with its own arm instant (`settled()`, `went_down_after()`).

### The lamp's link rows

`ConfigLinkService` publishes `state.link`: whether a phone is connected, and whether a prompt the flight state allows stands. Bravo, which has no glass, hands `AlarmService` `lamp_shows_link`, so its lamp also shows the prompt, a phone, a pairing request and an armed capture. A phone is any central but Bravo's pinned Go (`ports::Link::phones()`), because a Go connected all flight would hold the connected wink for good.

### The alarm steps

`alarm.h` holds the alarm setting's four steps, each adding to the one before. `Off` interrupts nothing. `Visual` takes the Go's glass to the radar and flickers Bravo's lamp. `Soft` and `Loud` also sound the buzzer at that loudness and pulse the motor. `Off` and `Visual` leave each target's level alone, so the radar still marks it and the NMEA stream still carries it.

`can_sound()` says whether a board has a buzzer or a haptic. A Go with neither ships on `Visual` and refuses `Soft` and `Loud` unless it is paired, when they read `BRAVO SOFT` and `BRAVO LOUD` and sound on Bravo alone (`../skyblip_go/settings_store.h`). The wire codes are the link registry's `alarm` enum.

## The partition, and the two logs on it

`services/record_pool.h` and `services/record_store.h` are the flash half of a log, and neither is a service. `RecordPool` is the partition, with one `store::SectorAllocator` and one boot scan. `RecordStore` is one ring on it: recovery, append, the session index, the erase and the answers to the Log endpoint. The product owns one pool and two stores: the flights store under `FlightLogService` and the diagnostics store under `CaptureService`.

### When flash may be touched

`RecordPool::book_window()` books every claim, erase, header and page program through the `FlashWindow` the product hands the pool, because only the product knows what a stalled pass costs it. The window answers true when `cost_ms` of work fits the pass that began at `pass_ms`, and books that work, so the next piece in the same pass starts after it. The Go's rule is in [`../skyblip_go/README.md`](../skyblip_go/README.md), "When flash may be touched".

Records go to flash a page at a time: `append()` writes the records that end inside one 256 B page in one call, booked at one `kSlotWriteCostMs` per page it touches. The capture writes at most `CaptureService::kDrainCeilingRecords` records in one pass, and a bulk erase at most `kEraseCeilingSectors` sectors. Opening a session touches no flash: the first `append()` claims the sector, so the erase and the header land in a window too.

### How a capture ends

A capture session is named by the UTC second it opened, like a flight. It ends when the pilot stops it, the device powers off, or the allocator refuses a sector because the flights floor blocks the claim. Whichever it is, the last record is a `diagnostics::End`, and only that record makes the session read `closed`: a diagnostics slot has no CRC, so a session without one may have a torn tail (`../../core/diagnostics/README.md`). The store keeps the last two slots of the frontier sector in hand, one for the `diagnostics::Gap` that names the records still queued when the partition refused, one for the `End`.

A write the part refuses is a storage fault: it is counted on the pool, published on `state.capture`, and the record stays in the RAM ring for the next pass. A sector the allocator refuses ends the capture, and that is written down.

A long capture rotates rather than stopping. When it loses the sector it opened in, it writes a `Gap` plus a fresh `Boot` and `Config`, and the `list` reply marks the session `truncated` (`../../core/store/README.md`).

The flight log is the one service that drains `bus.log_rx`, because its gates are the flights gates: the link claim, the ground state and the prompt a destructive erase takes. It refuses `no_diagnostics` for a store this build has no service for, and `flights_only` for an erase aimed at the capture, whose ring the allocator recycles on its own.
