## What this codebase does

The C++ firmware of skyBlip, electronic conspicuity trackers for light aircraft on nRF52840 under Zephyr. The Go transmits and receives ADS-L and FLARM-compatible traffic on 868 MHz and warns the pilot. Bravo receives ADS-B on 1090 MHz and talks to a paired Go over Bluetooth. Each directory has a README that states its rules; read the one beside a file before judging it. `protocols/`, `core/` and `ui/` build on the host too, with a test suite and libFuzzer harnesses in `test/fuzz/`.

## Trust shape

- Bluetooth: no pairing, no bonding, anyone in range connects. Commands that change the unit or read its log stage a prompt the pilot must confirm on the device, unless the browser is a trusted browser (Ed25519 key stored in `comms::TrustedBrowsers`), and are refused `in_flight` while airborne (`core/comms/README.md`).
- Frames on the config, log and peer endpoints are one strict CBOR map; `cbor::Reader` checks the whole frame before anything reads it, and `link::host_keys()` refuses unknown keys.
- Updates: MCUboot verifies the image signature against keys built into the bootloader (`firmware/keys/`); the app refuses an image for another product (protected TLV) and, on release builds, an older one (`core/dfu/README.md`).
- Identity: each unit has its own Ed25519 key; it believes identity messages (register, unlock) only when signed by the server key in `firmware/keys/server.pub` (`core/identity/README.md`).
- Go-Bravo peer link pins the other side's address; active spoofing is outside the stated threat model.

## Threat model

Highest impact: anything in range (radio or Bluetooth) making a unit transmit false traffic, stop transmitting, or warn falsely, in flight. Next: installing an image or setting a configuration without the pilot's confirmation or a trusted browser, or bypassing the flight gate. Next: memory-safety bugs (out-of-bounds, integer overflow on a 32-bit target, unbounded loops) in code that parses radio frames, NMEA, CBOR or image TLVs. Next: forging a unit's identity or replaying an unlock.

## Project-specific patterns to flag

- A link command handler that changes the unit without going through the prompt or the trusted-browser check, or without the flight gate.
- A length or count read from a frame, a TLV or NMEA used before it is checked against the buffer, especially where `long` is 32 bits and `char` unsigned on the nRF52.
- A signature check whose message is not built by `core/identity/messages.cpp`, or one that compares secrets with `memcmp`.

## Known false-positives

- Code under `simulator/`, `test/` and `scenarios/` runs on the host only.
- The Go-Bravo link has no cryptographic authentication on purpose (`core/comms/README.md`, The peer endpoint).
