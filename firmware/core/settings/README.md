# core/settings

Two things no product decides: the address this device claims on the air, and the framing its settings survive a power cycle in.

`address.h` holds skyBlip's ADS-L registry assignment, which a copy replaces with its own (`firmware/README.md`, Before you ship a copy). `address_of(serial)` is `kSkyblipManufacturerPrefix` followed by the 16-bit serial. `on_air(unit, aircraft_address)` picks the address on air (The ICAO address, below).

Issue 2 F.2.2 gives the table 64 entries: 0 is random/privacy, 1 to 4 are reserved, 5 ICAO (`adsl::kAddressTableIcao`), 6 FLARM, 7 OGN-Tracker, 8 FANET, and 9 upwards are manufacturer pages. Each page is an 8-bit prefix plus a 16-bit base address that `registry@ads-l.aero` assigns and the manufacturer must fill densely.

We transmit table 58 (`kSkyblipAddressMappingTable`), manufacturer page 49, with prefix 0x00 (`kSkyblipManufacturerPrefix`, [skyblip.eu/ads-l/registry](https://skyblip.eu/ads-l/registry)). Pages number from 9, so page 49 is value 58 in the 6-bit field. The 16 bits below the prefix are the unit's serial, which skyblip.eu assigns densely from 1 at activation and signs into an unlock code the unit verifies at every boot (`core/identity/README.md`). Serials are dense because F.2.2 asks for it and because derived ones collide: truncated to 16 bits, two of n units share an address with a probability of about n^2 / 131072, roughly 7 % at 100 units.

Before activation a unit has no address, so `AirAddress` is an optional, not a number with a sentinel. Three layers refuse on their own: the radio service arms neither receiver nor transmitter, `timing::TransmitGate` wants an address first and names it `ADDRESS` (`../timing/README.md`, The transmit gate), and `protocol::from_own()` builds no frame. Table 0 is not used: F.2.4 privacy mode requires a random address at every start-up.

The address is not a setting: the serial is the unlock code's, and the table and the prefix are constants. `go::Settings` has no field for them, and `apply_json` ignores a patch naming `address` or `address_mapping_table`. The `get` reply reports `address` once there is one and `address_mapping_table` always. Both stay the unit's own while an ICAO address goes on air, because the serial names the unit: its Bluetooth name, skyblip.eu's fleet and the self-test page all read it.

`blob.h` is a version byte, a payload and a CRC32 over both. `sealed()` checks that CRC over whatever length was stored, so a product tells a layout written by a later image from a torn sector without knowing either layout. The fields, defaults and refusals belong to the product, in `products/skyblip_go/settings.{h,cpp}`, so two products keep different settings on one framing. The companion link's side is `products/common/settings_store.h`.

## The ICAO address

ADS-L F.2.3, as the PDF prints it: "For this setting, the ICAO address shall be configurable and entered by the user of the ADS-L system, thereby overriding the address given by the manufacturer. This is the preferred mode for aircraft that have an ICAO address (Mode-S transponder code). This setting shall be revertible by user configuration. For aircraft that concurrently transmit using other e-conspicuity systems such as ADS-B, or aircraft that use a Mode-S transponder, it shall use this type of address to allow for disambiguation and attribution of the different radio transmissions on the receiver side. The ADS-L system shall enforce this by disallowing inconsistent user configurations."

The Go's `aircraft_address` is that setting (`products/skyblip_go/README.md`, Aircraft address). While it holds an address, `on_air()` puts it under table 5 on every payload: the Traffic, the Status and the name in payload 66, so a receiver files all three with the aircraft's ADS-B and Mode-S replies. Clearing it puts table 58 and the serial back from the next burst. That every payload changes table is our reading, and EASA is being asked to confirm it.

Three things stay the unit's own under an ICAO address:

- Activation. `on_air()` gives no address to a unit with no serial: F.2.3 overrides the address on air, not the signed serial that allows a unit on air.
- The schedule. `timing::Transmitter` keys the cycle start, the slot instants and the deferral draws on the serial (`core/timing/README.md`), so setting or clearing the address mid-flight moves no burst in time or channel.
- The traffic filter. The table refuses the unit's own address under table 58 and `aircraft_address` under table 5, so a ground station relaying us is refused under either (`core/traffic/README.md`, Our own aircraft).

The FLARM ID, `flarm_id`, is no address on air. It names a FLARM fitted in the aircraft for the traffic table to refuse (`products/skyblip_go/README.md`, FLARM ID), and the unit never transmits under it, because table 6 IDs are FLARM's to assign and F.2.3 names only the ICAO address as one a user may enter.

The setting changes in flight like any other (`core/comms/README.md`, The flight gate). F.2.4 forbids toggling privacy mode in flight, but F.2.3 has no such rule, and a pilot who finds the address wrong in flight should be able to correct it.

The Go refuses the configurations it can know to be inconsistent, on the link and on the glass alike:

- An address no aircraft holds. 0 is unset. ICAO assigns neither 24 zeros nor 24 ones (Annex 10 Volume III, Appendix to Chapter 9, 5.2 f), so `icao_assignable()` refuses 0xFFFFFF and the link answers `out_of_range`. A stored 0xFFFFFF loads as unset, the rest of the blob kept.
- An address that is not the transponder Bravo hears. On the ground a paired, live Bravo may name the aircraft's own transponder (`products/skyblip_bravo/README.md`, Own transponder) in `bus::State::own_transponder`. Any other address is then refused, `not_own_transponder` over the link. A clear is never refused, because F.2.3 asks for the setting to be revertible, and a `set` that leaves `aircraft_address` out is not refused for one already stored.

Rules considered and not made:

- Refusing an unset address while Bravo hears a transponder, because it would block a clear. Adopting Bravo's address unasked would put an unconfirmed address on air, and a neighbour on the apron can be as loud as our own transponder. The Go proposes it instead (`products/skyblip_go/README.md`, Aircraft address).
- Refusing an address outside the blocks ICAO has allocated to States. ICAO amends that table, and a unit built before an assignment would refuse it.
- Refusing an address another target in the traffic table uses. Our own transponder is that target, so the rule would refuse the right address.
- Refusing an address because the type or the callsign does not match it. Nothing on board can check either.
