# protocols

The radio protocols, each a component another project can copy with its tests and build with nothing from skyBlip. [`adsl/`](adsl/README.md) is ADS-L 4 SRD-860 Issue 2, [`adsb/`](adsb/README.md) reads the 1090 MHz extended squitter, [`alptas/`](alptas/README.md) is the FLARM-generation frame, and [`common/`](common/README.md) holds the wire arithmetic two or more of them share.

A stack holds what its specification decides. What skyBlip chose stays outside it, in `core/` and `products/`, as adapters that turn a stack's plain values into skyBlip's model and back: `core/protocol/adsl.*` turns own-ship into an ADS-L Traffic payload, `core/protocol/adsb_observation.*` turns an ADS-B track into an `AircraftObservation`, and so on (`core/protocol/README.md`). Where this split was not obvious, the rule decided it: a figure the specification prints goes in the stack, and a figure skyBlip measured or chose stays out.

## The boundary

A file under `protocols/` includes only the C++ standard library, its own directory and `protocols/common/`. It never includes `core/`, `ports/`, `hardware/`, `products/`, `runtime/`, `ui/`, `simulator/` or Zephyr, and one stack never includes another. `core/` and `products/` may include `protocols/`. `scripts/check_protocol_boundary.py` holds every include to this rule, `scripts/test_check_protocol_boundary.py` checks the checker, and CI runs both.

A stack speaks in its own plain types: `adsl::Traffic`, `adsl::StatusReport` and `adsl::UplinkTarget`, `adsb::Message` and `adsb::Track`, `alptas::Position`, and `wire::Status` for a frame it refuses. No `model::`, `events::` or `settings::` type appears inside one.

Each stack carries a `README.md` saying what it implements and what it leaves out, a `version.h`, its code and a `test/` folder. Its tests include nothing from skyBlip either. They use the vendored doctest and `common/test/channel.h`, the bit-error channel the frames are pushed through.

## Taking a stack into another project

Copy `protocols/common/` and `protocols/<stack>/` under one include root, keeping the `protocols/` directory, since every include is written from that root as `#include "protocols/adsl/packet.h"`. Compile every `.cpp` in the two directories with C++17. The code allocates nothing on the heap and needs neither exceptions nor RTTI. For the tests, add every `.cpp` under their `test/` folders, doctest's header and a main that defines `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`.

`make test-protocols` does exactly that for each stack, in a copy under `build/protocols-alone/` that holds the stack, `common/` and `vendor/doctest` and nothing else, so an include that reaches outside fails to compile there. CI runs it. The normal `make test` binary still runs every stack's tests with the rest of the suite, and the device build compiles every stack into the image (`cmake/skyblip.cmake`).

## Versions

Each stack's `version.h` is the one source of its version. `kVersion` is a `major.minor.patch` string and `kSpecification` names what the stack implements. A version moves only when a change alters what that protocol puts on air or reads off it: its frames, its encoding, its channels, its timing or its transmit schedule. `firmware/README.md` says how the protocol versions relate to the firmware version. All three stay below 1.0.0 until the protocol is ready for production.

Each product lists the protocols it carries in `products/<product>/PROTOCOLS`, one stack name per line. Every release names the protocol versions each product carries, in a pre-release's notes and in a production release's `PROTOCOLS.md`, read out of `version.h` by `lib/radio_protocols.rb`. A product that reports them over a link includes `protocols/<stack>/version.h` for both constants.

`adsl/version.h` is the version of skyBlip's ADS-L implementation, not the Protocol Version field every ADS-L packet carries on air (E.1.1). The ADS-L declaration of conformity names firmware versions, in the template's "Firmware Version" field (`docs/reference/ads-l_4_srd860_issue_2.md:1659`). The ADS-L version decides which firmware releases a declaration still covers, because each release lists the protocol versions it carries.

A version covers its stack's directory and `protocols/common/`, so a change to either is a decision on that version. A few skyBlip files outside them still change what goes on air or is read off it, and a change to one of them is a decision on the version too. The list is for review: no CI check holds a change to it yet.

| Version | skyBlip files outside the stack that change what it puts on air or reads off it |
|---|---|
| ADS-L | `core/protocol/adsl.*` (own-ship to Traffic, accuracy from DOP, the Status values), `core/protocol/callsign.*` (payload 66), `core/protocol/adsl_uplink.*`, `core/protocol/air.*` (the shared-sync receiver and the radio's M-band buffer), `core/timing/slot.*` (the dwell map), `core/timing/transmit.*` (instants clipped to the dwells, the callsign's second and window, the completion guard), `core/flight/extrapolate.*`, `core/flight/state.*` (airborne detection), `core/settings/address.h` (AMT 58 and the manufacturer prefix), `products/skyblip_go/services/radio.*` (`RadioService`), and the conducted power, antenna and modulation settings in `hardware/parts/sx1262/sx1262.h` |
| ADS-B | `core/protocol/adsb_observation.*`, `core/protocol/adsb_category.*`, `hardware/parts/lr2021/`, and `products/skyblip_bravo/services/adsb.*` |
| ALP-TAS | `core/protocol/alptas.*`, the ALP-TAS half of `core/protocol/air.*`, `adsl_category_to_alptas_type` in `core/protocol/nmea_out.*`, and `products/common/services/traffic.*` |
