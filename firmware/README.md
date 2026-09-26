# firmware

The C++ tree: `core/` is the portable logic, `ports/` the roles a product needs from a board, `hardware/` the parts and platforms that fill them, `ui/` the drawing kit, `products/` the pages and services that make one device what it is, `boards/` the Zephyr board, `test/` the host suite, `simulator/` the world it flies in. Each directory carries its own README for what it decides.

A file lives in the narrowest layer that decides its content. A spec, a physical law or arithmetic is `core/`; what the core needs done for it is `ports/`; a datasheet is `hardware/`; a schematic is `boards/`; which pages exist, what a press means and which settings a pilot gets is the product's; pixels that any product would draw the same way are `ui/`.

`ports/` is a port layer, not a hardware abstraction layer, and it is named for what it is: it declares the roles the core needs filled, in the core's own vocabulary, and `hardware/` is where a part or a platform fills one. A port is what the core calls; what the world does to a sensor arrives as an `events::` value on a queue instead, and `hardware/README.md` is where that half of the contract is written down. Register code belongs there, never here. `core/` and `ui/` compile with no framework headers at all, which is what buys the host suite and the WASM simulator; Zephyr is used freely below `ports/` and never above it.

`make test` runs the host suite, `make test-sanitize` runs it again under ASan and UBSan in `build/sanitize/`, `make simulator` builds the terminal one, `scripts/build_local.sh` from the repo root builds the device image.

`.clang-format` and `.clang-tidy` live here rather than at the root because this is the only C++ in the repository, and both tools read the nearest config above the file they are given. `make tidy` runs the linter over what the host build compiles; CI runs the same target.

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
| none | the SI base unit, whole | `alt_m`, `speed_mps`, `freq_hz` |
| `_ms`, `_us`, `_s` | milliseconds, microseconds, seconds | `at_ms`, `tx_at_us`, `flight_seconds` |
| `_mm_s` | millimetres per second | `climb_mm_s` |
| `_1e7`, `_e8`, `_e2`, `_e1` | scaled by that power of ten | `lat_1e7`, `climb_e8`, `hdop_e2` |
| `_q` | quarter units, the ADS-L velocity encoding | `speed_q` |
| `_c9` | ADS-L's 9-bit course, 360 degrees over 512 counts | `track_c9` |
| `_dbm`, `_mv`, `_mpa`, `_pa` | dBm, millivolts, millipascals, pascals | `rssi_dbm`, `battery_offset_mv`, `pressure_mpa` |
| `_dps` | degrees per second | `turn_dps` |

`core/units/units.h` carries the same units as types, and the conversions between them. It is what `ui/` reads in, because a page that prints knots and feet should be converting from a type rather than from a name. On the wire and in `bus::State` the suffix is the convention, because a struct that crosses a queue is a layout as well as a vocabulary.

## Naming a tuning constant

A constant that tunes a behavior reads `k<Subject><Mechanism><Unit>`, unit last and always spelled: `kLandingHoldMs`, `kPresentFloorMs`, `kTargetForgetMs`. `scripts/check_tuning_names.py` holds the tree to it and CI runs it; [`docs/TUNING.md`](../docs/TUNING.md) is generated from the same parse, so every number below is listed with its value and the line that justifies it.

The mechanism is the half people get wrong, because four different things all read as "a delay" in conversation and behave nothing alike:

| Mechanism | What it means | Reset by | Example |
|---|---|---|---|
| `Hold` | evidence must persist this long before the state flips | contrary or missing evidence | `kLandingHoldMs`, `kTogetherHoldMs` |
| `Settle` | wait this long after an event before trusting what follows | the next event | `kFirstFixSettleMs`, `kRailSettleMs` |
| `Floor`, `Ceiling` | a lower or upper bound, in the unit named | nothing | `kPresentFloorMs`, `kImplausibleFloorMv` |
| `Period` | fixed cadence | nothing | `kLogRecordPeriodMs` |
| `Window` | the span a measurement or a budget is taken over | nothing | `kTurnWindowMs` |
| `MaxAge` | past this an input stops counting as evidence | a fresh report | `kAlertMaxAgeMs` |
| `Stale` | past this a reading leaves the glass | a fresh reading | `kIndicatedStaleMs` |
| `Forget` | past this the record itself is dropped | a fresh report | `kTargetForgetMs` |
| `Samples`, `Fixes` | consecutive readings that must agree | one reading the other way | `kCutoffSamples`, `kConvergedFixes` |

Hysteresis is not on the list and never becomes a constant: it is the gap between two named thresholds, like the 12.0 m/s a takeoff needs and the 1.0 m/s a landing does, and naming the gap would be a third number nobody reads.

The subject comes first because a mechanism on its own is ambiguous where it is used: `kForgetMs` forgets what? Two of those existed, in two namespaces, 30 s each by coincidence. A constant inside a class takes the class as its subject, so `ScreenService::kPresentFloorMs` needs no more. `Delay`, `Grace`, `Debounce` and `Steady` are spelled as one of the words above, so that a grep for every hold in the tree finds every hold.

Out of scope, deliberately: `hardware/`, `boards/` and `ports/`. A datasheet's 2.6 s busy time is the part's figure under the part's name, and renaming it would hide where it came from.

## License

GPL-3.0-only, see [`LICENSE`](LICENSE). This directory is the copyleft one: the rest of the repository is MIT, and code cannot travel from here to there. The WASM the simulator page loads is built from these sources and carries this license with it.
