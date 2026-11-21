# protocols

Each directory here is a radio protocol stack that another project can copy with its tests and build with nothing from skyBlip. [`adsl/`](adsl/README.md) is ADS-L 4 SRD-860 Issue 2. [`adsl_edge/`](adsl_edge/README.md) is what the Go sends on ADS-L's frame outside Issue 2's scope. [`adsb/`](adsb/README.md) reads the 1090 MHz extended squitter. [`alptas/`](alptas/README.md) is ALP-TAS, skyBlip's reading of the 2024 air frame. [`common/`](common/README.md) holds the wire arithmetic two or more stacks share, and the figures of EN 300 220, the standard every 868 MHz stack transmits under.

A figure or formula the specification prints goes in the stack. What skyBlip measured or chose stays in `core/` and `products/`, in adapters between a stack's plain values and skyBlip's model (`core/protocol/README.md`). Where the specification sets a rule and leaves a choice, the stack holds the rule: `adsl::period_s()` is G.1.16's rate, and `core/timing/` picks the seconds and channels.

## The boundary

A file under `protocols/` includes only the C++ standard library, its own directory and `protocols/common/`. One stack never includes another, with one exception: `adsl_edge/` builds on ADS-L's frame, so it may include `adsl/`, and no stack may include `adsl_edge/`. `core/` and `products/` may include `protocols/`. `make test-protocols` holds each stack to this rule by building it from a copy that holds nothing else, and `scripts/check_protocol_boundary.py` refuses an angle header outside the C++ standard library, which that copy still finds on the host.

A stack speaks in its own plain types, such as `adsl::Traffic` or `adsb::Message`, never a `model::`, `events::` or `settings::` type. It reads a frame through one `receive()`, which runs the CRC and answers `wire::Status`. Each stack carries a `README.md`, a `version.h`, its code and a `test/` folder that uses only doctest and `common/test/channel.h`.

## Taking a stack into another project

Copy `protocols/common/` and `protocols/<stack>/` under one include root, keeping the `protocols/` directory, since includes read `#include "protocols/adsl/packet.h"`. Compile with C++17 and GCC or Clang, because the ADS-L packet's bit fields are laid out as GCC lays them and the stacks call `__builtin_*`. Build for a little-endian host, because XXTEA copies the frame's bytes into 32-bit words. The code needs no heap, no exceptions and no RTTI. For the tests, add `test/`, doctest and a main defining `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`. `make test-protocols` builds each stack this way in CI.

## Versions

`version.h` holds `kVersion`, `major.minor.patch`, and `kSpecification`, what the stack implements. A version moves only when a change alters a rule conformance rests on: frames, encoding, channels, timing, transmit rules, or a reading the frozen suite pins. A change inside those rules moves the firmware version alone (`firmware/README.md`). A stack stays at 0.1.0 until it is ready for production. `adsl/version.h` is skyBlip's implementation version, not the Protocol Version field of E.1.1.

Each product lists its stacks in `products/<product>/PROTOCOLS`. Every release names their versions in its notes and `PROTOCOLS.md` (`lib/radio_protocols.rb`), and a unit reports them to the companion link's `protocols` command (`core/comms/README.md`), so a unit can be matched to a declaration of conformity, which names firmware versions.

### Freezing

A version covers its stack and the `protocols/common/` files it includes. A frozen stack's `version.lock` holds the SHA-256 of every file under the roots its conformance rests on, and `scripts/check_protocol_freeze.py` fails in CI when one changed without a reseal. Run `python3 scripts/check_protocol_freeze.py --reseal <stack>` after moving `kVersion`, or after a change that leaves what goes on air and comes off it as it was, and say which in the pull request.

ADS-L is sealed at 0.1.0, and until it is declared 1.0.0 a change to a sealed file is resealed without a bump. Its roots are the stack, both halves of its conformance suite, the EN 300 220-1 suite and the device files that set what goes on air. The rest of the firmware decides what the Go sends inside the rules, and the sealed cases in `test/adsl/` hold it to them.

`adsl_edge/` is not frozen, and `PROTOCOLS` leaves it out because no declaration covers payload 66. When an ADS-L issue defines the payload, its record moves into the ADS-L stack.

ADS-B and ALP-TAS are not frozen yet. A change to one of these files outside the stack is a decision on its version:

| Version | skyBlip files outside the stack that change what it puts on air or reads off it |
|---|---|
| ADS-B | `core/protocol/adsb_observation.*`, `core/protocol/adsb_category.*`, `core/units/units.h`, `arctangent` in `core/util/intmath.*` (the track a velocity gives), `adsl::kAddressTableIcao` where `adsb_observation.cpp` writes it, `hardware/parts/lr2021/`, and `products/skyblip_bravo/services/adsb.*` |
| ALP-TAS | `core/protocol/alptas.*`, the ALP-TAS half of `core/protocol/air.*`, `adsl_category_to_alptas_type` in `core/protocol/nmea_out.*`, and `products/skyblip_go/services/traffic.*` (`decode_alptas`) |
