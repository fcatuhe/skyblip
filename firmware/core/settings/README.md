# core/settings

Two things that are not a product's to decide: the address space this device claims on the air, and the framing its settings survive a power cycle in.

`address.h` is ADS-L's, not ours. It holds the table this device transmits under and the prefix its addresses start with, and says how an address is made: `address_of(serial)` is `kManufacturerPrefix` followed by the 16-bit serial.

Issue 2 F.2.2 gives the table 64 entries: 0 is random/privacy, 1 to 4 are reserved, 5 ICAO, 6 FLARM, 7 OGN-Tracker, 8 FANET, and 9 upwards are manufacturer pages, each one an 8-bit prefix plus a 16-bit base address that `registry@ads-l.aero` assigns and the manufacturer is required to fill densely.

We transmit table 58 (`kAddressMappingTable`), manufacturer page 49, which `registry@ads-l.aero` assigned on 5 October 2026 with manufacturer prefix 0x00 (`kManufacturerPrefix`, [skyblip.eu/ads-l-registry](https://skyblip.eu/ads-l-registry)). F.2.2 numbers the manufacturer pages from 9, so page 49 is value 58 in the 6-bit field. The 16 bits below the prefix are the unit's serial, which skyblip.eu assigns densely from 1 at activation and signs into an unlock code the unit verifies at every boot (`core/identity/README.md`). Dense because F.2.2 asks for it and because derived ones collide: truncated to 16 bits, two units collide with a probability of about n^2 / 131072, roughly 7 % at 100 units and 50 % at 300.

Before activation a unit has no address at all, which is why `AirAddress` is an optional and not a number with a sentinel. Three layers refuse on their own, each with its own case: the radio service arms neither the receiver nor the transmitter, `timing::ownship_transmits()` wants an address beside the fix and the clock, and `protocol::from_own()` builds no frame without one. There is no fallback address any more: the 0x5BCAFE that stood in for a dead chip-id read went with the chip-id address itself. Table 0 is not used either: F.2.4 privacy mode requires a random address chosen at every start-up.

None of this is anybody's to change, because none of it is stored as a setting. The serial is the unlock code's, the table and the prefix are constants. Neither is a field of `go::Settings`, so there is no flash copy to migrate, no menu row, and nothing for `apply_json` to accept: a patch naming `address` or `address_mapping_table` is ignored the way any key with no reader is, and the `get` reply reports `address` once there is one and `address_mapping_table` always. The cost is F.2.3, the ICAO override, which this device does not offer and `test/adsl/test_presentation.cpp` states as a skipped case rather than a silent gap.

`blob.h` is a version byte, a payload and a CRC32 over both. `sealed()` checks that CRC over whatever length was stored, which is how a product tells a layout written by a later image from a torn sector without knowing either layout. It knows nothing about fields: what a product stores, what it defaults to, what it refuses and how an older layout migrates forward are the product's, in `products/skyblip_go/settings.{h,cpp}`. That is where `units`, `aircraft_type` and `callsign` belong, because they are what a pilot of that device can change and what `schemas/config.v1.schema.json` pins.

The split is what lets two products keep different settings without two copies of the framing, and it is why the CRC is tested here against a payload that is not anybody's `Settings`.

The companion link's side of the same line is `core/comms/config_store.h`: `comms::ConfigService` reads and writes the link's CBOR frames, and the product hands it the calls that turn its own struct into their fields and a `set` back into it.
