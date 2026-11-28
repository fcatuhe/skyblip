# firmware

The C++ tree. Each directory carries its own README for what it decides:

| Directory | What it holds |
|---|---|
| `protocols/` | the radio protocols, each a stack another project can take with its tests |
| `core/` | the portable logic |
| `disciplines/` | what a way of flying, such as soaring, adds to the alarm, one pack per discipline |
| `ports/` | the roles a product needs from a board |
| `hardware/` | the parts and platforms that fill those roles |
| `ui/` | the drawing kit |
| `products/` | the pages and services that make one device what it is |
| `boards/` | the Zephyr boards |
| `test/` | the host suite |
| `simulator/` | the world the host build flies in |

[`EXTENDING.md`](EXTENDING.md) lists where a contribution goes, the contract each place holds it to and what the maintainer keeps.

Put a file in the narrowest layer that decides its content. A radio protocol's specification is `protocols/`, which includes nothing of skyBlip's ([`protocols/README.md`](protocols/README.md)). Any other spec, physical law or arithmetic is `core/`. What one way of flying adds to the alarm, such as predicting a circling glider, is `disciplines/` ([`disciplines/README.md`](disciplines/README.md)). What the core needs done for it is `ports/`. A datasheet is `hardware/` and a schematic is `boards/`. Which pages exist, what a press means and which settings a pilot gets belong to the product. Pixels any product would draw the same way are `ui/`.

Put a service that more than one product runs in `products/common/`, in namespace `common` ([`products/common/README.md`](products/common/README.md)), not in `runtime/`. `runtime/` is the loop every service is written against: `Service`, `Context`, `Loop`, the watchdog's feed decision and the pass budgets. It decides nothing about traffic, a phone, an alarm or a device. No file in `products/common/` includes a product's header. A product hands a shared service the settings it needs as a small struct the product fills. The config link loads and writes the settings, so it takes the product's `common::SettingsStore` instead. The device build adds `products/common/` beside the other shared layers in `cmake/skyblip.cmake`.

`ports/` declares the roles the core needs filled, in the core's own vocabulary. It is a port layer, not a hardware abstraction layer, and holds no register code. `hardware/` fills the roles. The core calls a port. What the world does to a sensor arrives as an `events::` value on a queue instead ([`hardware/README.md`](hardware/README.md)). `protocols/`, `core/`, `disciplines/` and `ui/` compile with no framework headers, so the host suite and the WASM simulator can build them. Zephyr is used freely below `ports/` and never above it.

| Command | What it does |
|---|---|
| `make test` | runs the host suite, the protocol stacks' tests among it |
| `make test-protocols` | builds and runs each stack alone, from a copy holding nothing else |
| `make test-sanitize` | runs the suite under ASan and UBSan, in `build/sanitize/` |
| `make fuzz` | runs the libFuzzer harnesses in `test/fuzz/` with clang |
| `make fuzz-<name>` | runs one harness: `nmea`, `air`, `adsb`, `adsb_assembly`, `link`, `peer_bravo` or `peer_go` |
| `make tidy` | runs clang-tidy over what the host build compiles, as CI does |
| `make browser` | compiles the WASM that the Rails app's `/briefing/simulator` loads from a release, and bundles `scenarios/*.json` into the `skyblip-simulator-scenarios.json` beside it (`simulator/README.md`, The browser) |
| `make watch` | rebuilds and reruns the host tests on every save, or the WASM with `WATCH=browser`, through `watchexec` |
| `make clean` | removes `build/` |
| `make ... ABI=ilp32` | builds any of the above for i386, where `long` is 32 bits as on the nRF52 |
| `scripts/build_local.sh`, from the repo root | builds the device images |

`.clang-format` and `.clang-tidy` live here because this is the only C++ in the repository, and both tools read the nearest config above the file.

## Versions

Four versions move apart. [`VERSION`](VERSION) is the firmware version, one for every product. It moves with anything a product does, such as a page, an alarm or a power saving. A `v<version>` tag releases every product at it (`.github/workflows/release.yml`). Each `products/<product>/VERSION` is a link to it, because Zephyr reads the version from the application's own directory.

The other three are the radio protocols' versions, a `kVersion` string beside the `kSpecification` it implements: `protocols/adsl/version.h`, `protocols/adsb/version.h` and `protocols/alptas/version.h`. Each started at 0.8.0, the firmware version of the first release. A protocol version that moves takes the firmware version of the release that first carries the change. A protocol version moves only when a change alters a rule its conformance rests on: the stack's frames, encoding, channels, timing or transmit rules, or a reading the frozen conformance suite pins. A change inside those rules, such as which second a burst takes, moves the firmware version alone. Each product lists its stacks in `products/<product>/PROTOCOLS`, one per line. Every release names the protocol versions each product carries, in a pre-release's notes and in a production release's `PROTOCOLS.md` (`lib/radio_protocols.rb`). A unit names its own over the companion link's `protocols` command. `protocols/README.md` says what hangs on them and which skyBlip files outside a stack still change what it puts on air.

## Before you ship a copy

Some of what this tree compiles in names skyBlip and nobody else. A product built from it replaces each of these with its own before a unit leaves the bench:

| Identity | Where | What a copy does |
|---|---|---|
| ADS-L address table 58 and manufacturer prefix 0 | `core/settings/address.h` (`kSkyblipAddressMappingTable`, `kSkyblipManufacturerPrefix`) | applies to the ADS-L registry (`registry@ads-l.aero`) for its own: the registry assigned these to skyBlip, ticket #228794 (`protocols/adsl/README.md`) |
| USB Product ID 0x5AFE under pid.codes' Vendor ID 0x1209 | `test/twister/meshtracker_x1/prj.conf`, the X1 smoke image. The products run no USB device | requests its own PID from pid.codes. No host tool, udev rule or WebSerial filter in this repository matches on it |
| USB manufacturer and product strings `skyBlip`, `skyBlip Bravo` | the same `prj.conf` | names its own product |
| Bluetooth names `skyBlip Go`, `skyBlip Go+`, `skyBlip Bravo`, with the serial behind them | `CONFIG_BT_DEVICE_NAME` in each `prj.conf`, `products/skyblip_go/product.h`, `products/skyblip_bravo/name.h`, `core/identity/` | names its own product, so a companion app does not take it for a skyBlip |
| Bluetooth service and characteristic UUIDs, base `69c21300-0187-4204-a91d-02eb8858b440` | `hardware/platform/zephyr/skyblip_uuid.h` | draws its own 128-bit base |
| MCUboot signing keys | `keys/development.pub.pem`, `keys/production.pub.pem` | signs with keys of its own (`keys/README.md`) |
| MCUboot image vendor name `skyblip.eu` | `CONFIG_MCUBOOT_IMGTOOL_UUID_VID_NAME` in `products/common/prj.conf` | sets its own domain, from which imgtool derives the image's vendor UUID |
| The skyblip.eu server key, id `8c06724d` | `keys/server.pub`, compiled in by `cmake/skyblip.cmake` | runs its own activation server with its own key, or leaves `CONFIG_SKYBLIP_ACTIVATION` off |

## Naming a boolean

A boolean field says which kind of fact it is in its suffix, never in a prefix, because `has_` and `have_` both read as "somebody has something".

| Suffix | Fact | Example |
|---|---|---|
| `_valid` | the value beside it is a real reading, taken and trustworthy now | `fix_valid`, `lateral_valid`, `flight_time_valid` |
| `_fitted` | the hardware is on this unit, whatever it is currently reporting | `inclinometer_fitted` |

A state is neither, and takes the adjective the domain already uses: `airborne`, `charging`, `transmitting`, `flyable`, `battery_low`. Fitted and valid are two fields, because a page draws them differently: a plain T-Echo has no inclinometer and prints none, and a Plus whose sensor has not answered yet prints the empty cage.

`has_` survives as a call on a container, not as a field: `ports::has(capabilities, Capability::Rf)`, `Inventory::has_i2c_address(0x28)`.

## Naming a quantity

A number carries its unit in its suffix, because the field is read far from the code that scaled it and a reader assumes SI.

| Suffix | Unit | Example |
|---|---|---|
| none | the SI base unit, whole | `altitude_m`, `speed_mps`, `frequency_hz` |
| `_ms`, `_us`, `_s` | milliseconds, microseconds, seconds | `at_ms`, `tx_at_us`, `flight_seconds` |
| `_mm_s` | millimetres per second | `climb_mm_s` |
| `_cm`, `_ft` | centimetres, feet | `hfom_cm`, `altitude_ft` |
| `_kt`, `_fpm` | knots, feet per minute | `speed_kt`, `climb_fpm` |
| `_mkt` | thousandths of a knot | `speed_mkt` |
| `_1e7`, `_e8`, `_e2`, `_e1` | scaled by that power of ten | `latitude_1e7`, `climb_e8`, `hdop_e2` |
| `_q` | quarter units, the ADS-L velocity encoding | `speed_q` |
| `_c9` | ADS-L's 9-bit course, 360 degrees over 512 counts | `track_c9` |
| `_c7`, `_c10` | ADS-B's 7-bit and 10-bit angles, 360 degrees over 128 and 1024 counts | `track_c7`, `heading_c10` |
| `_cpr` | a CPR-encoded coordinate, a 17-bit fraction of its zone | `latitude_cpr` |
| `_dbm`, `_mv`, `_mpa`, `_pa` | dBm, millivolts, millipascals, pascals | `rssi_dbm`, `battery_offset_mv`, `pressure_mpa` |
| `_centi_dbm`, `_centi_db`, `_centi_dbd` | hundredths of a dBm, of a dB, of a dB over a half-wave dipole | `threshold_centi_dbm`, `receive_gain_centi_dbd` |
| `_deg`, `_cdeg` | degrees, hundredths of a degree | `track_deg`, `track_cdeg` |
| `_dps`, `_cdps` | degrees per second, hundredths of a degree per second | `turn_dps`, `turn_cdps` |
| `_mg` | thousandths of a g | `lateral_mg` |
| `_c`, `_dc` | whole degrees Celsius, tenths of a degree Celsius | `die_temperature_c`, `die_dc` |
| `_dbhz` | dB-Hz, a carrier-to-noise density | `cn0_dbhz` |
| `_px` | pixels on the glass | `kDashPx`, `kMinLeaderPx` |

`core/units/units.h` carries the same units as types, with the conversions between them. `ui/` reads those types, so a page that prints knots and feet converts from a type rather than from a name. On the wire and in `bus::State` the suffix is the convention, because a struct that crosses a queue is a layout.

## Naming a tuning constant

A constant that tunes a behavior reads `k<Subject><Mechanism><Unit>`, unit last and always spelled: `kLandingHoldMs`, `kPresentFloorMs`, `kTargetForgetReports`. CI runs `scripts/check_tuning_names.py` to hold the tree to it. [`docs/TUNING.md`](../docs/TUNING.md) is generated from the same parse and lists every constant with its value and the comment that justifies it.

Pick the mechanism from this table. A hold, a settle, a floor and a period all read as "a delay" in conversation and behave nothing alike:

| Mechanism | What it means | Reset by | Example |
|---|---|---|---|
| `Hold` | evidence must persist this long before the state flips | contrary or missing evidence | `kLandingHoldMs`, `kTogetherHoldMs` |
| `Settle` | wait this long after an event before trusting what follows | the next event | `kFirstFixSettleMs`, `kRailSettleMs` |
| `Floor`, `Ceiling` | a lower or upper bound, in the unit named | nothing | `kPresentFloorMs`, `kImplausibleFloorMv` |
| `Period` | fixed cadence | nothing | `kLogRecordPeriodMs` |
| `Window` | the span a measurement or a budget is taken over | nothing | `kTurnWindowMs` |
| `Damping` | the time constant of the first-order lag a reading is shown through: a step shows 63% of itself after it | nothing | `kIndicatedRateDampingMs`, `kIndicatedDampingSamples` |
| `MaxAge` | past this an input stops counting as evidence | a fresh report | `kAlertMaxAgeMs` |
| `Stale` | past this a reading leaves the glass | a fresh reading | `kIndicatedStaleMs` |
| `Forget` | past this the record itself is dropped | a fresh report | `kTargetForgetReports`, `kCallsignForgetS` |
| `Samples`, `Fixes` | consecutive readings that must agree | one reading the other way | `kLevelSamples`, `kConvergedFixes` |

Hysteresis is usually no constant. It is the gap between two named thresholds, like the 12.0 m/s a takeoff needs and the 1.0 m/s a landing does, and naming the gap would add a third number nobody reads. The exception is a gap measured once and shared by every step of a ladder, named for what was measured: `kRecoveryMarginMv` is the spread a parked cell reads across, and each battery level is left upwards by that much above the step it was entered under (`core/power/README.md`).

The subject comes first because a mechanism alone is ambiguous: `kForgetMs` forgets what? A constant inside a class takes the class as its subject, so `ScreenService::kPresentFloorMs` needs no more. Spell `Delay`, `Grace`, `Debounce` and `Steady` as one of the words above, so a grep for every hold finds every hold.

`hardware/`, `boards/` and `ports/` are out of scope. A datasheet's 2.6 s busy time keeps the part's name for it, so a reader can find where it came from.

## Naming a part of the device

Each word below names one thing, in code and in prose. A word that names two things hides a bug where the two differ.

| Word | What it names | Not |
|---|---|---|
| glass | what the pilot reads and presses through: the drawing surface, its pages and its prompts. `go::Glass` is a `ui::Bitmap` the size the board states | the part behind it |
| panel | the e-paper part: its lot and ID, its controller, its refreshes, its rails and its lines. `parts::Panel`, `panel_full_refreshes` | what a page draws |
| `Display` | the port the panel fills, `ports::Display` | |
| screen | `ScreenService`, which decides what the glass shows and when the panel refreshes, and its `Screen` record | |
| bitmap | `ui::Bitmap<W,H>`, a canvas that owns its bytes, at any size | |
| lamp | the one status light, showing one `indication::Colour` or none | the backlight |
| LED | a part a board fills the lamp with, one per colour. Only `boards/` and `hardware/` drive one | |
| annunciator | the buzzer and the haptic, the two voices a call is played on, `ports::Annunciator` | |
| haptic | the role that shakes, `ports::Haptic`. Its verbs are `vibrate()` and `still()` | `vibro` |
| motor | the part that shakes, on a pin or behind a DRV2605 | |
| sign | the lamp and the motor pulsed together as the unit comes on or goes off, `indication::kSignOn` and `kSignOff` | a signature |
| signature | a cryptographic signature: Ed25519, MCUboot or ADS-L's | a sign, a panel ID |
| cell | the Li-ion cell. `cell_mv` is its voltage, valid only when the reading is the cell | the USB rail |
| battery | the subsystem and the pilot's word for it: `BatteryState`, `battery_percent`, `BATTERY` on the glass | |
| divider | the resistor divider the ADC reads: the cell off the cable, the USB rail on it. `read_divider_mv()`, `BatterySample::divider_mv` | the cell |
| park | stop a part and release its lines, ready for the rails to drop | `show(Colour::None)`, which only darkens |
| pass | one run of the service loop over every service | |

## Naming a test case

A test case states a claim. `scripts/behavior_index.py` generates [`docs/BEHAVIOR.md`](../docs/BEHAVIOR.md) and the pages under `docs/behavior/` from the claims. Name a case `subject: what holds`, as in `button: a stable press emits exactly one event`, because the subject groups the cases on their area's page. Open every test file with a comment that says what it pins down. A comment above a case is quoted under its claim.

A claim is main or it is an edge case. Mark a main case with the `main` test suite, the only one the suite uses, and leave every other case unmarked:

```cpp
TEST_CASE("button: a stable press emits exactly one event" * doctest::test_suite("main")) {
```

| Tier | What it covers |
|---|---|
| main | what the device does when used as intended, which a pilot, a reviewer or a declaration of conformity relies on: what goes on air and when, what is received and shown, what an alarm means, what the screen and the controls do, what activation, update, pairing and settings do, and the rules a standard imposes on what the device sends. State it as a user or a reviewer would recognize it |
| edge case or detail | refusals of malformed input that a pilot never sees, that change nothing on air and that no standard requires, boundaries and saturation, clock and counter wraps, recovery mechanics, regressions found by fuzzing, internal mechanics, formatting details and harness checks |

A failure, a refusal or a guard is main when a pilot sees it or relies on it for safety, when it changes what goes on air, or when a standard requires it. How the firmware recovers from it is a detail. When in doubt, leave the case unmarked.

Keep main small, about 10 to 15% of an area's cases. Give a subject a user would recognize at least one main case when one qualifies, and give a purely internal subject none. The pages whose cases are named after a clause of a standard are different: `adsl`, `adsb`, `en300220` and the `protocols-*-conformance` pages, where nearly every subject is a clause. There a case is main when it states a rule of the clause on what the device puts on air or reads off it, and the 10 to 15% still holds. Each area's page lists its main claims first and folds the rest, and `docs/BEHAVIOR.md` lists every main claim. `build/skyblip_tests -ts=main` runs the main cases alone. `scripts/behavior_index.py` refuses a name without a subject and a claim on either side of `: `, a file with no opening comment, a test suite other than `main`, and a skipped case marked main, because a skipped case claims nothing.

## License

MIT, like the rest of the repository: see [`LICENSE`](../LICENSE). [`NOTICE.md`](../NOTICE.md) lists what an image is built from that is not ours, each with its license: Zephyr and its modules, the toolchain's libraries, Monocypher and the Bosch hub image.
