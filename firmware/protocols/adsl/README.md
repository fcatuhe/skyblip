# protocols/adsl

ADS-L 4 SRD-860 Issue 2, EASA's iConspicuity link, from the specification the declaration of conformity names: [`docs/reference/ads-l_4_srd860_issue_2.md`](../../../docs/reference/ads-l_4_srd860_issue_2.md), with the PDF beside it winning where the two disagree. `version.h` says so in `kSpecification`. Everything here is what the specification decides, and it builds and passes its tests with nothing but `protocols/common/` and doctest (`../README.md`). The ADS-L declaration of conformity rests on this directory: what goes on air and what is read off it is here, or in a skyBlip file `../README.md` lists.

## What is in

| File | Clauses | What it holds |
|---|---|---|
| `packet.{h,cpp}` | E, F.2, G.1, G.2 | `Packet`, the 24 bytes the CRC covers: the network header, the F.2 header and its sender address, the G.1 Traffic and G.2 Status fields at the offsets the tables print, XXTEA scrambling (E.2), the CRC-24 (E.3.1) and the erasure repair a receiver runs before it. `Traffic` and `encode_traffic()`, `decode_traffic()` are the Traffic payload as plain values. `StatusReport` and `encode_status()` are the Status payload. `timestamp_code()` is G.1.1, `hfom_cm()` is G.1.14's HFOM, and the NACp and GVA tables are G.1.14 and G.1.15. The F.2.2 address tables and G.1.4's "no emergency" are named constants |
| `physical.h` | C.2, C.4, D.1.1 | the two M-band channels and the O band, the M-band modulation and its 14 dBm e.r.p. ceiling, and the sync word with D.1.1's length byte behind it |
| `uplink.{h,cpp}`, `reed_solomon.{h,cpp}` | C.4, D.1.1, E.3.2, G.3 | the O-band HDR modulation, sync and burst, the G.3 Traffic Uplink packet with up to ten verbatim Traffic payloads and its 10 s age rule, and E.3.2's RS(255,223) code with its roots and zero prefix |
| `flight_state.{h,cpp}` | G.1.2, G.1.16 | the flight state codes, the Undefined a craft able to hover announces on the ground, and the report period each state asks for |
| `versioning.{h,cpp}` | B.5, G.2.1 | the Max. Protocol Version a packet declares, and the test that makes a client close enough to write for |
| `transmit.h` | C.2, C.2.5, C.5, G.1.16, G.2.11 | the rules a transmitter keeps: the uplink and direct slots, a frame's air time, the Traffic schedule for a ground second the integrator chooses, channel alternation, the 500 ms nav age and the Status period |
| `version.h` | | `kVersion` and `kSpecification` |

## What is out, and where it is

These are skyBlip's choices, built on this stack:

- own-ship as a Traffic payload: the extrapolation to the instant the timestamp names, which fields claim invalid without a fix, the VFOM taken from VDOP by G.1.14's model, and the NACv read off NACp as SoftRF does (`core/protocol/adsl.*`)
- which second of ten a unit owns, the instant drawn inside the direct slot, where the Status goes, and the hour's air time declared as the channel-access route in place of §D.3's listen-before-talk (`core/timing/transmit.*`, `core/timing/channel.*`)
- the address table 58 and the manufacturer prefix (`core/settings/address.h`), and airborne detection (`core/flight/state.*`)
- the single-radio dwell map, which decides when each channel is heard and so the M-band receive class the Status declares (`core/timing/slot.*`)
- the callsign, OGN's payload 66 that F.2.1 leaves to OGN: its record, its window in slot 1's tail and its second (`core/protocol/callsign.*`, `core/timing/transmit.*`)
- the dual-system receiver that names ADS-L or ALP-TAS from one shared sync window (`core/protocol/air.*`)
- B.5's bookkeeping: each target's Max. Protocol Version and the lowest any close one reads (`core/traffic/table.*`), and how own Traffic is re-coded for Issue 1 (`core/protocol/adsl.*`)
- `RadioService` and the SX1262 settings that put a frame on air

## What is not implemented

Each gap is a skipped case in the conformance suite, written as the clause requires (below): O-band LDR (C.3, D.1.2), the secure signature (E.4), FIS-B (G.4), Remote Identification (G.5), and privacy mode (F.2.4). `build/skyblip_tests --no-skip --test-case="ADS-L*"` prints them.

## Public API

Everything is in namespace `skyblip::adsl`.

- `kAddressTableRandom` to `kAddressTableManufacturerLast`, `manufacturer_page_table()`, `kEmergencyNone`, `kUereCm`, `hfom_cm()`
- `Packet` with `reset()`, the field accessors, `scramble()`, `descramble()`, `set_crc()`, `check_crc()` and `correct()`, and `kSyncWord`, `kFrameBytes`
- `Traffic`, `Integrity`, `encode_traffic()`, `decode_traffic()`, `timestamp_code()`, `lowest_version()`, `issue2_category()`
- `StatusReport`, `ReceiveClass`, `receive_class()`, `conspicuity::`, `encode_status()`
- `Uplink`, `UplinkStatistics`, `uplink_length()`, `uplink_payload_bytes()`, `uplink_record_current()`, `encode_o_band()`, `ReedSolomon255`
- `declared_max_version()`, `close_client()`, `kCloseHorizontalM`, `kCloseVerticalM`, `kCloseTcpaS`, `Packet::kMaxProtocolVersion`
- `FlightState`, `state_from()`, `announced_state()`, `reduced_rate()`, `report_period_s()`, `flies_at_no_ground_speed()`
- `on_schedule()`, `channel_in()`, `period_s()`, `kUplinkSlotStartMs`, `kUplinkSlotEndMs`, `kDirectStartMs`, `kDirectEndMs`, `kAirTimeMs`, `kFixMaxAgeMs`, `kStatusPeriodS`
- `kVersion`, `kSpecification`

## The transmit rules

`transmit.h` holds the rules the specification sets for a transmitter, and leaves every choice inside them to the integrator. §C.5 gives the ground stations 200 to 450 ms of each UTC second and the aircraft 450 to 1000 (`kUplinkSlotStartMs` to `kDirectEndMs`), and a burst starts and completes inside the direct slot. §C.2 puts a 24-byte frame on air for 4.64 ms, so `kAirTimeMs` books 5. §G.1.16 asks for at least 1 Hz airborne and 0.1 Hz on the ground (`report_period_s()`), and no navigation data older than 500 ms (`kFixMaxAgeMs`), measured to the top of the transmit second because the encoder carries the fix forward to the instant the burst names. §G.2.11 asks for a Status at least every 20 s (`kStatusPeriodS`).

`on_schedule()` says whether a second carries Traffic: every second airborne, and on the ground the one second of ten the integrator hands it, so the ground population spreads by the integrator's choice of second and a unit that reboots comes back to the same one. `channel_in()` is §C.2.5's alternation from one transmission to the next, counted by the clock, `(utc_s / period) & 1`: counting a device's own transmissions instead would put it out of step with the grid the first time anything refused a burst.

The rest is the integrator's, and skyBlip's choices are in `core/timing/README.md`: which second of ten a unit owns, the instant drawn inside the direct slot, where the Status goes, and the channel-access route a device declares in place of §D.3's listen-before-talk.

## The Status payload

`encode_status()` writes §G.2's payload type 3: what a device can hear and process, so a peer or a ground station can plan around it. Its network header says protocol version 1, because the Status payload first appears in Issue 2 (below). `receive_class()` turns a share of the direct slot heard into §G.2.2's classes: more than 5% is Occasional, more than 40% Partial and more than 80% Full. What skyBlip declares in each field, and why, is in `core/protocol/README.md`.

The payload is 12 bytes, header included. The frame still carries 20 payload bytes, as a Traffic frame does, with the last 8 left zero, so it goes out behind the same sync word and the same length byte. §D.1.1 lets a receiver read the length, but a receiver that checks it as 0x18 in the sync tail, as skyBlip's does (`core/protocol/air.cpp`), would not hear a 16-byte Status frame, and the OGN code SoftRF carries builds every frame at 24 bytes (`libraries/OGN/ads-l.h:17-22`).

## Which issue a payload is

§E.1.1 asks every packet to carry the lowest protocol version whose encoding of its payload is the same, Issue 1 being 0 and Issue 2 being 1. `lowest_version()` reads it off the clear packet, and `encode_traffic()` and `encode_status()` set it. Issue 1's payload table names only Traffic, type 2, so every other payload is version 1: the Status, and OGN's payload 66, which skyBlip sends its name in. A Traffic payload is version 0 unless it carries a code whose meaning Issue 2 changed. These are every code of G.1 that changed between `docs/reference/ads-l_4_srd860_issue_1.md` and Issue 2:

| Field | Code | Issue 1 | Issue 2 | Version |
|---|---|---|---|---|
| G.1.5 latitude, longitude | 0x800000 | a position, 90 S or 180 W | no 2D fix (Issue 1 marked that 0xFFFFFF) | 1 |
| G.1.7 altitude | 0x3FFF | 61112 m | invalid | 1 |
| G.1.7 altitude | 0x3FFE | 61104 m | 61104 m or more | 0, the limit reads true in both |
| G.1.8 ground speed | 0xFF | 238 m/s | invalid | 1 |
| G.1.8 ground speed | 0xFE | 236 m/s | 236 m/s or more | 0 |
| G.1.9 vertical rate | 0x1FF | -119 m/s | invalid | 1 |
| G.1.9 vertical rate | 0x1FE | -118 m/s | -118 m/s or less | 0 |
| G.1.3 category | 3 | rotorcraft | light rotorcraft | 0, a light rotorcraft is a rotorcraft |
| G.1.3 category | 6 | ultralight | ultralight and motorized hang-glider | 0, a motorized hang-glider is flown as an ultralight |
| G.1.3 category | 7 | hang-glider or paraglider | paraglider | 0, a paraglider is one of the two |
| G.1.3 category | 14 to 17 | reserved | model plane, heavy rotorcraft, hang-glider, paramotor | 1 |
| G.1.15 GVA | 3 | VFOM under 15 m | VFOM under 10 m | 0, under 10 m is under 15 m |
| G.1.15 GVA | 2 | VFOM under 45 m | VFOM from 10 to 45 m | 0 |

Every other G.1 table only writes its bounds out, and G.1.10, G.1.16 and G.1.17 add rules and no codes. A code marked 0 in the last column is one an Issue 1 receiver still reads as a true statement, so it stays at version 0 for the widest reach. The clause cases in `test/conformance/test_network.cpp` pin one row each.

## Soft versioning

§B.5 lets the specification move on without splitting the air. Every packet carries the version it is written in (§E.1.1, above), and the Status carries the highest version its sender reads (§G.2.1, `Packet::kMaxProtocolVersion`, 1 here). `declared_max_version()` reads either: the Status field, or for any other packet the version it came in, which §B.5 tells a receiver to assume until it hears a Status. `close_client()` is §B.5's test for a neighbour own Traffic has to stay readable to: under 1 km horizontally and 800 m vertically, or under 800 m vertically and less than 30 s from the closest point of approach, that time being the horizontal distance over the closure rate. A Traffic packet then goes out in the lowest version any close neighbour reads. Which targets are tracked, and how a Traffic payload is re-coded for an older issue, are skyBlip's, in `core/traffic/README.md` and `core/protocol/README.md`.

## The Traffic Uplink

`Uplink::encode()` builds G.3's payload type 4 for a ground station: the station's own address in the F.2 header, then for each target the 30-bit address of the aircraft that sent it and its 120-bit Traffic payload copied byte for byte, then the padding F.2.5 asks for. The network header says version 1, because Issue 1 has no uplink, and sets the error control bit, because the parity is E.3.2's and not the CRC. `Uplink::decode()` counts the records from the D.1.1 length byte, as G.3 asks, refuses a length no whole number of records can spell, and refuses a record with no address or no position before it becomes a target. `uplink_record_current()` is G.3's age rule: a record is current up to 10 s after the instant its G.1.1 timestamp names, and never before it. The longest packet, ten records, is 229 bytes behind its length byte and takes 9.4 ms of air at C.4's 200 kbps (`kUplinkBurstUs`).

E.3.2 computes the parity over the network header and the scrambled data, prefixed with zeros to 223 bytes, and sends no padding. `ReedSolomon255` is the code the clause prints: the field 1+x2+x3+x4+x8 (`kFieldPolynomial`, 0x11D), the primitive element 11 counted in index form, alpha^11, as CCSDS 131.0 and Karn's fec library count it (`kPrimitiveElement`), and 32 roots from the 121st (`kFirstRoot`). The conformance case compares it to a bit-by-bit encoder written from those three numbers and to one block the reedsolo library encoded.

## Reed-Solomon, `reed_solomon.cpp`

| Our name | Textbook name |
|---|---|
| `kCodewordBytes`, `kDataBytes`, `kParityBytes` | n, k, n - k |
| `antilogarithm_`, `logarithm_` | the exp and log tables of GF(2^8) |
| `generator_` | the generator polynomial g(x) |
| `locator`, `previous_locator` | Berlekamp-Massey's Λ(x) and B(x) |
| `register_length` | L |
| `shift` | m, the steps since B(x) last changed |
| `discrepancy`, `last_discrepancy` | δ and b |
| `evaluated` | Λ evaluated in the Chien search |
| `evaluator` | the error evaluator Ω(x) |
| `locator_derivative` | Λ'(x), the formal derivative in Forney's formula |
| `position_power`, `inverse_power` | X_k and its inverse |
| `root_power(i)` | beta^i, where beta is alpha^11 |

## The conformance suite

The specification is read clause by clause in two halves. `test/conformance/` holds the cases this component satisfies on its own, one file per subpart, and `firmware/test/adsl/` holds the cases that need skyBlip around it: the simulator, a product or the dwell map. `scripts/check_adsl_clauses.py` checks the two together: every `ADS-L.4.SRD860.x.y` heading in the specification has at least one case in one half, and every case names a real clause. `firmware/test/adsl/README.md` says how a case is named and what a skipped one means.

Where the specification prints code or a worked example, the test carries it rather than restating our answer: E.2's XXTEA and E.3.1's CRC-24 are transcribed from the clause and compared against `protocols/common/`, G.1.6's exponential encoder is implemented from the algorithm as written, and G.1.7 to G.1.9 assert the exact codes of the clauses' example tables. The G.1 case reads every field at the bit offsets of the payload table, so a field that moves by one bit fails there first.

The unit tests beside it take the packet over a bit-error channel and count detected failures apart from silent miscorrections (`test/test_packet.cpp`), pin a frame byte for byte, and check the uplink's code, the transmit rules and the hour's air time.
