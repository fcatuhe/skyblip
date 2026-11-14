# core/settings

Two things that are not a product's to decide: the address space this device claims on the air, and the framing its settings survive a power cycle in.

`address.h` holds skyBlip's ADS-L registry assignment: the table this device transmits under and the prefix its addresses start with, which a copy replaces with its own (`firmware/README.md`, Before you ship a copy), and says how an address is made: `address_of(serial)` is `kSkyblipManufacturerPrefix` followed by the 16-bit serial. It also says which address goes on air: `on_air(unit, own_icao)` is the aircraft's ICAO address under table 5 while the pilot has set one, and the unit's own address under table 58 otherwise (The ICAO address, below).

Issue 2 F.2.2 gives the table 64 entries: 0 is random/privacy, 1 to 4 are reserved, 5 ICAO, 6 FLARM, 7 OGN-Tracker, 8 FANET, and 9 upwards are manufacturer pages, each one an 8-bit prefix plus a 16-bit base address that `registry@ads-l.aero` assigns and the manufacturer is required to fill densely.

`adsl::kAddressTableIcao` (`protocols/adsl/packet.h`) is table 5. Every ADS-B address is an ICAO address, and F.2.3 has an ADS-L unit in an aircraft that also carries a transponder send that address under table 5. The traffic table refuses our own aircraft's ICAO address under it (`core/traffic/README.md`, "Our own aircraft").

We transmit table 58 (`kSkyblipAddressMappingTable`), manufacturer page 49, which `registry@ads-l.aero` assigned on 5 October 2026 with manufacturer prefix 0x00 (`kSkyblipManufacturerPrefix`, [skyblip.eu/ads-l/registry](https://skyblip.eu/ads-l/registry)). F.2.2 numbers the manufacturer pages from 9, so page 49 is value 58 in the 6-bit field. The 16 bits below the prefix are the unit's serial, which skyblip.eu assigns densely from 1 at activation and signs into an unlock code the unit verifies at every boot (`core/identity/README.md`). Dense because F.2.2 asks for it and because derived ones collide: truncated to 16 bits, two units collide with a probability of about n^2 / 131072, roughly 7 % at 100 units and 50 % at 300.

Before activation a unit has no address at all, which is why `AirAddress` is an optional and not a number with a sentinel. Three layers refuse on their own, each with its own case: the radio service arms neither the receiver nor the transmitter, `timing::ownship_transmits()` wants an address beside the fix and the clock, and `protocol::from_own()` builds no frame without one. There is no fallback address any more: the 0x5BCAFE that stood in for a dead chip-id read went with the chip-id address itself. Table 0 is not used either: F.2.4 privacy mode requires a random address chosen at every start-up.

None of this is anybody's to change, because none of it is stored as a setting. The serial is the unlock code's, the table and the prefix are constants. Neither is a field of `go::Settings`, so there is no flash copy to migrate, no menu row, and nothing for `apply_json` to accept: a patch naming `address` or `address_mapping_table` is ignored the way any key with no reader is, and the `get` reply reports `address` once there is one and `address_mapping_table` always. Both stay the unit's own while an ICAO address goes on air, because the serial is what names the unit: its Bluetooth name, skyblip.eu's fleet and the self-test page all read it.

`blob.h` is a version byte, a payload and a CRC32 over both. `sealed()` checks that CRC over whatever length was stored, which is how a product tells a layout written by a later image from a torn sector without knowing either layout. It knows nothing about fields: what a product stores, what it defaults to, what it refuses and how an older layout migrates forward are the product's, in `products/skyblip_go/settings.{h,cpp}`. That is where `units`, `aircraft_type` and `callsign` belong, because they are what a pilot of that device can change and what the `config` frame of `schemas/link.v1.json` describes.

The split is what lets two products keep different settings without two copies of the framing, and it is why the CRC is tested here against a payload that is not anybody's `Settings`.

The companion link's side of the same line is `core/comms/config_store.h`: `comms::ConfigService` reads and writes the link's CBOR frames, and the product hands it the calls that turn its own struct into their fields and a `set` back into it.

## The ICAO address

ADS-L F.2.3, as the PDF prints it: "For this setting, the ICAO address shall be configurable and entered by the user of the ADS-L system, thereby overriding the address given by the manufacturer. This is the preferred mode for aircraft that have an ICAO address (Mode-S transponder code). This setting shall be revertible by user configuration. For aircraft that concurrently transmit using other e-conspicuity systems such as ADS-B, or aircraft that use a Mode-S transponder, it shall use this type of address to allow for disambiguation and attribution of the different radio transmissions on the receiver side. The ADS-L system shall enforce this by disallowing inconsistent user configurations."

The Go's `own_icao` is that setting (`products/skyblip_go/README.md`, Own ICAO). While it holds an address, `on_air()` puts it under table 5 on every payload the unit sends: the Traffic, the Status and the name in OGN's payload 66. A receiver then files all three under the same target as the aircraft's ADS-B and its Mode-S replies. Clearing it, 0 over the link or a confirmed clear on the glass, puts table 58 and the serial back from the next burst. The reading that every payload changes table, and none stays on the manufacturer's page, is the owner's, and EASA is being asked to confirm it.

Three things stay the unit's own under an ICAO address:

- Activation. `on_air()` gives no address to a unit with no serial, whatever `own_icao` holds, so an unactivated unit still transmits nothing. F.2.3 overrides the address on air, not the signed serial that puts a unit on air at all.
- The schedule. `timing::Transmitter` is configured with the unit's own address, so the 20 s cycle's start, `c = 2 * (serial % 10)`, the instant drawn in each slot and the deferral draws all key on the serial (`core/timing/README.md`). A pilot who sets or clears the address mid-flight changes the address on the next burst and moves no burst in time or channel, so §C.2.5's alternation and G.2.11's Status period run on unbroken. Serials are also unique and dense, which is what spreads a fleet exactly evenly over the ten start seconds.
- The traffic filter. The table still refuses the unit's own address under table 58, and refuses `own_icao` under table 5, so a ground station relaying us is refused under either (`core/traffic/README.md`, Our own aircraft).

A setting changed in flight takes effect as any other setting does. The link refuses a `set` `in_flight` unless it comes from a trusted browser, and the glass's menu is open in flight (`core/comms/README.md`, The flight gate). F.2.4 forbids toggling privacy mode in flight. F.2.3 has no such rule, and a pilot who finds the address wrong in flight should be able to correct it.

"Disallowing inconsistent user configurations": the Go refuses the configurations it can know to be wrong, on the link and on the glass alike.

- An address no aircraft holds. 0 is unset. ICAO assigns neither 24 zeros nor 24 ones (Annex 10 Volume III, Appendix to Chapter 9, 5.2 f), so `icao_assignable()` refuses 0xFFFFFF, and the link answers `out_of_range`. A blob an older build stored with 0xFFFFFF comes back unset, the rest of it kept.
- An address that is not the transponder Bravo hears. On the ground, a paired, live Bravo may name the transponder it takes for the aircraft's own (`products/skyblip_bravo/README.md`, Own transponder), and `bus::State::own_transponder` carries it. While it does, any other address is refused: `not_own_transponder` over the link, and on the glass the field stays open under `REFUSED: BRAVO HEARS 3C6586`. A clear is never refused, because F.2.3 asks for the setting to be revertible, and a `set` that leaves `own_icao` out is not refused for one already stored.

What was considered and not made a rule:

- Refusing an unset address while Bravo hears a transponder. That is the inconsistency F.2.3 names most directly, but refusing an absence would block a clear. Adopting Bravo's address on its own would put an address on air that nobody confirmed, and a neighbour on the apron can be as loud as our own transponder. The Go proposes it instead, once a boot and on the `AIRCRAFT` menu, and warns while neither the Go nor Bravo has one (`products/skyblip_go/README.md`, Own ICAO).
- Refusing an address outside the blocks ICAO has allocated to States, or in the blocks Annex 10 reserves for future use. ICAO moves that table by amendment, and a unit that refused an address its pilot was assigned after the firmware was built would leave that aircraft under the wrong address. The repository holds no such table, and F.2.3 asks for none.
- Refusing an address another target in the traffic table already uses. Our own transponder is that target, as heard by a Bravo or relayed by a ground station, so the rule would refuse the right address.
- Refusing an address because the type or the callsign does not match it. Nothing on board can check either.

`test/adsl/test_presentation.cpp` holds F.2.3's cases, and `test/products/test_icao_address.cpp`, `test_settings_own_icao.cpp` and `test_menu_own_icao.cpp` the device's.
