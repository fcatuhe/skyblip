# firmware

The C++ tree: `protocols/` is the radio protocols, each a stack another project can take with its tests, `core/` is the portable logic, `ports/` the roles a product needs from a board, `hardware/` the parts and platforms that fill them, `ui/` the drawing kit, `products/` the pages and services that make one device what it is, `boards/` the Zephyr boards, `test/` the host suite, `simulator/` the world it flies in. Each directory carries its own README for what it decides.

A file lives in the narrowest layer that decides its content. What a radio protocol's specification decides is `protocols/`, and it includes nothing of skyBlip's ([`protocols/README.md`](protocols/README.md)); a spec, a physical law or arithmetic is otherwise `core/`; what the core needs done for it is `ports/`; a datasheet is `hardware/`; a schematic is `boards/`; which pages exist, what a press means and which settings a pilot gets is the product's; pixels that any product would draw the same way are `ui/`.

A service that more than one product runs lives in `products/common/`, in namespace `common` ([`products/common/README.md`](products/common/README.md)). It does not go in `runtime/`. `runtime/` is the loop every service is written against: `Service`, `Context`, `Loop`, the watchdog's feed decision and the pass budgets. It decides nothing about traffic, a phone or an alarm. A shared service decides product content: the cadence a phone's map is fed at, which claim turns it on, which settings it reads. That belongs to the products layer, shared by more than one product. No file in `products/common/` includes a product's header. A product hands a shared service what it reads of the product's settings as a small struct the product fills, and the claims it makes as `common::Feature` bits in its own `kFeatures`. The config link also loads and writes the settings, so it takes the product's `common::SettingsStore` instead. The host build, the tuning index and the clang-format job already walk `products/*`, and the device build adds `products/common/` beside the other shared layers in `cmake/skyblip.cmake`. Every later move of a service out of a product follows the same rule.

`ports/` is a port layer, not a hardware abstraction layer, and it is named for what it is: it declares the roles the core needs filled, in the core's own vocabulary, and `hardware/` is where a part or a platform fills one. A port is what the core calls; what the world does to a sensor arrives as an `events::` value on a queue instead, and `hardware/README.md` is where that half of the contract is written down. Register code belongs there, never here. `protocols/`, `core/` and `ui/` compile with no framework headers at all, which is what buys the host suite and the WASM simulator; Zephyr is used freely below `ports/` and never above it.

`make test` runs the host suite, the protocol stacks' tests among it, `make test-protocols` builds and runs each stack alone from a copy holding nothing else, `make test-sanitize` runs it again under ASan and UBSan in `build/sanitize/`, `make fuzz` runs the libFuzzer harnesses in `test/fuzz/` with clang, `ABI=ilp32` builds any of them for i386 where `long` is 32 bits as on the nRF52, `make simulator` builds the terminal one, `scripts/build_local.sh` from the repo root builds the device images.

`.clang-format` and `.clang-tidy` live here rather than at the root because this is the only C++ in the repository, and both tools read the nearest config above the file they are given. `make tidy` runs the linter over what the host build compiles; CI runs the same target.

## Versions

Four versions move apart. [`VERSION`](VERSION) is the firmware version, one for every product: it moves with anything a product does, such as a page, an alarm or a power saving, and a `v<version>` tag releases every product at it (`.github/workflows/release.yml`). Each `products/<product>/VERSION` is a link to it, because Zephyr reads the version from the application's own directory.

The other three are the radio protocols' versions, one per stack under `protocols/`: `protocols/adsl/version.h` for ADS-L, `protocols/adsb/version.h` for ADS-B and `protocols/alptas/version.h` for ALP-TAS, each a `kVersion` string beside the `kSpecification` it implements. A protocol version moves only when a change alters what that protocol puts on air or reads off it: its frames, its encoding, its channels, its timing or its transmit schedule. Each product lists the protocols it carries in `products/<product>/PROTOCOLS`, one stack name per line. Every release names the protocol versions each product carries, in a pre-release's notes and in a production release's `PROTOCOLS.md` (`lib/radio_protocols.rb`). `protocols/README.md` says what hangs on them and which skyBlip files outside a stack still change what it puts on air.

## Before you ship a copy

This tree is open source, and some of what it compiles in names skyBlip and nobody else. A product built from it replaces each of these with its own before a unit leaves the bench. They are listed here and none of them changes:

| Identity | Where | What a copy does |
|---|---|---|
| ADS-L address table 58 and manufacturer prefix 0 | `core/settings/address.h` (`kSkyblipAddressMappingTable`, `kSkyblipManufacturerPrefix`) | applies to the ADS-L registry (`registry@ads-l.aero`) for its own: the registry assigned these to skyBlip, ticket #228794 (`protocols/adsl/README.md`) |
| USB Product ID 0x5AFE under pid.codes' Vendor ID 0x1209 | `products/skyblip_go/prj.conf`, `products/skyblip_bravo/prj.conf`, `test/twister/meshtracker_x1/prj.conf` | requests its own PID from pid.codes. No host tool, udev rule or WebSerial filter in this repository matches on it |
| USB manufacturer and product strings `skyBlip`, `skyBlip Go`, `skyBlip Bravo` | the same `prj.conf` files | names its own product |
| Bluetooth names `skyBlip Go`, `skyBlip Go+`, `skyBlip Bravo`, with the serial behind them | `CONFIG_BT_DEVICE_NAME` in each `prj.conf`, `products/skyblip_go/product.h`, `products/skyblip_bravo/name.h`, `core/identity/` | names its own product, so a companion app does not take it for a skyBlip |
| Bluetooth service and characteristic UUIDs, base `69c21300-0187-4204-a91d-02eb8858b440` | `hardware/platform/zephyr/skyblip_uuid.h` | draws its own 128-bit base |
| MCUboot signing keys | `keys/development.pub.pem`, `keys/production.pub.pem` | signs with keys of its own (`keys/README.md`) |
| MCUboot image vendor name `skyblip.eu` | `CONFIG_MCUBOOT_IMGTOOL_UUID_VID_NAME` in `products/skyblip_go/prj.conf` and `products/skyblip_bravo/prj.conf` | sets its own domain, from which imgtool derives the image's vendor UUID |
| The skyblip.eu server key, id `8c06724d` | `keys/server.pub`, compiled in by `cmake/skyblip.cmake` | runs its own activation server with its own key, or leaves `CONFIG_SKYBLIP_ACTIVATION` off |

## Naming a boolean

A boolean field says which kind of fact it is in its suffix, never in a prefix. `has_` and `have_` both read as "somebody has something" and the two were two lines apart in one struct before this rule existed.

| Suffix | Fact | Example |
|---|---|---|
| `_valid` | the value beside it is a real reading, taken and trustworthy now | `fix_valid`, `lateral_valid`, `flight_time_valid` |
| `_fitted` | the hardware is on this unit, whatever it is currently reporting | `inclinometer_fitted` |

A state is neither, and takes the adjective the domain already uses: `airborne`, `charging`, `transmitting`, `flyable`, `battery_low`. A device that measures something it is not measuring right now is two facts and two fields, because a page draws them differently: a plain T-Echo has no inclinometer and prints none, a Plus whose sensor has not answered yet prints the empty cage.

`has_` survives as a verb on a container that is asked a question, where it is a call and not a field: `ports::has(capabilities, Capability::Rf)`, `Inventory::has_i2c_address(0x28)`.

## Naming a quantity

A number carries its unit in its suffix, for the same reason a boolean carries its kind in one: the field is read far from the code that scaled it, and a fixed-point value with no suffix is a bug waiting for a reader who assumes SI.

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

`core/units/units.h` carries the same units as types, and the conversions between them. It is what `ui/` reads in, because a page that prints knots and feet should be converting from a type rather than from a name. On the wire and in `bus::State` the suffix is the convention, because a struct that crosses a queue is a layout as well as a vocabulary.

## Naming a tuning constant

A constant that tunes a behavior reads `k<Subject><Mechanism><Unit>`, unit last and always spelled: `kLandingHoldMs`, `kPresentFloorMs`, `kTargetForgetReports`. `scripts/check_tuning_names.py` holds the tree to it and CI runs it; [`docs/TUNING.md`](../docs/TUNING.md) is generated from the same parse, so every number below is listed with its value and the line that justifies it.

The mechanism is the half people get wrong, because four different things all read as "a delay" in conversation and behave nothing alike:

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

Hysteresis is not on the list and is usually no constant: it is the gap between two named thresholds, like the 12.0 m/s a takeoff needs and the 1.0 m/s a landing does, and naming the gap would be a third number nobody reads. The exception is a gap measured once and shared by every step of a ladder, which is named for what was measured: `kRecoveryMarginMv` is the spread a parked cell reads across, and each battery level is left upwards by that much above the step it was entered under (`core/power/README.md`).

The subject comes first because a mechanism on its own is ambiguous where it is used: `kForgetMs` forgets what? Two of those existed, in two namespaces, 30 s each by coincidence. A constant inside a class takes the class as its subject, so `ScreenService::kPresentFloorMs` needs no more. `Delay`, `Grace`, `Debounce` and `Steady` are spelled as one of the words above, so that a grep for every hold in the tree finds every hold.

Out of scope, deliberately: `hardware/`, `boards/` and `ports/`. A datasheet's 2.6 s busy time is the part's figure under the part's name, and renaming it would hide where it came from.

## License

MIT, like the rest of the repository: see [`LICENSE`](../LICENSE). [`NOTICE.md`](../NOTICE.md) lists what an image is built from that is not ours, each with its license: Zephyr and its modules, the toolchain's libraries, Monocypher and the Bosch hub image.
