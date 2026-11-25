# protocols/adsl

ADS-L 4 SRD-860 Issue 2, EASA's iConspicuity link, read from [`docs/reference/ads-l_4_srd860_issue_2.md`](../../../docs/reference/ads-l_4_srd860_issue_2.md), the specification the declaration of conformity names (`kSpecification` in `version.h`). The PDF beside it wins where the two disagree. This directory holds what the specification decides, in namespace `skyblip::adsl`, and builds and passes its tests with only `protocols/common/` and doctest. The declaration of conformity rests on it and on the files `version.lock` freezes with it (`../README.md`, Versions).

## What is in

| File | Clauses | What it holds |
|---|---|---|
| `packet.{h,cpp}` | D.1.1, E, F.2, G.1, G.2 | `Packet`, the bytes the CRC covers, with the G.1 Traffic and G.2 Status fields at the offsets the tables print. XXTEA scrambling (E.2), the CRC-24 (E.3.1), the erasure repair, `receive()` and `receive_as_heard()`. The Traffic and Status payloads as plain values, `timestamp_code()` (G.1.1), and the HFOM, NACp and GVA tables (G.1.14, G.1.15) |
| `physical.h` | C.2, C.4, D.1.1 | the M-band channels, modulation and 14 dBm e.r.p. ceiling, the O band, each band's backoff interval, and the sync word with D.1.1's length byte behind it |
| `uplink.{h,cpp}`, `reed_solomon.{h,cpp}` | C.4, D.1.1, E.3, G.3 | the O-band HDR modulation and burst, the G.3 Traffic Uplink under either parity, and E.3.2's RS(255,223) code |
| `flight_state.{h,cpp}` | G.1.2, G.1.16 | the flight state codes and each one's report period |
| `soft_versioning.{h,cpp}` | B.5, G.2.1 | the Max. Protocol Version, the close-client test and `issue1_form()` |
| `transmit.h` | C.2, C.2.5, C.5, G.1.16, G.2.11 | the transmit rules below |
| `media_access.{h,cpp}` | D.3 | `media_access()`, the media access rule |
| `version.h` | | `kVersion` and `kSpecification` |

`Packet` and `Traffic` carry speed in 0.25 m/s (`speed_q`), vertical rate in 0.125 m/s (`climb_e8`), track in 360/512 degree steps (`track_c9`), and altitude in metres above the WGS-84 ellipsoid, never pressure altitude (G.1.7).

## What is out, and where it is

These are skyBlip's choices, built on this stack:

- G.1.2's Undefined for categories that cannot show they landed, and airborne detection (`core/flight/state.*`)
- own-ship as a Traffic payload, its invalid fields and accuracy claims (`core/protocol/adsl.*`)
- the schedule, the instant inside the direct slot, the air-time budget, the EN 300 220-1 parameters, the deferral draw and the CCA threshold (`core/timing/transmit.*`, `core/timing/channel.*`), and the dwell map behind the M-band receive class (`core/timing/slot.*`)
- which packets are forced and how a deferred burst is re-armed (`products/skyblip_go/services/radio_access.cpp`), and the channel assessment (`hardware/platform/zephyr/rf.h`)
- the address table and the ICAO address (`core/settings/address.h`, `on_air()`), and the configurations the Go refuses (`products/skyblip_go/settings.*`)
- the receiver that names ADS-L or ALP-TAS from one shared sync window (`core/protocol/air.*`)
- B.5's bookkeeping: each client's Max. Protocol Version and where it is, apart from the display (`core/traffic/versions.*`), and when own Traffic goes in its Issue 1 form, is held or is written again before it keys (`products/skyblip_go/services/radio_frames.cpp`)

## What the receiver refuses

The stack reads Issue 2 payloads and Issue 1 Traffic. `receive()` runs the erasure repair, then reads the E.1 header the CRC covers:

- `Crc`: no repair fits.
- `Invalid`: the header names the FEC, though an M-band frame is checked by its CRC, or the length is not the payload type's, 16 for a Status and 24 otherwise.
- `Unsupported`: a version above `Packet::kMaxProtocolVersion`, or key 1 or 2.
- Key 0 is descrambled. Key 3, which E.1.3 lets a constrained sender use, is read in the clear.

`Uplink::decode()` refuses a length no whole number of records spells under either parity, a CRC that does not check or a Reed-Solomon codeword it cannot correct, a header whose error control bit names the other parity, a version above 1, key 1 or 2, and a payload that is not type 4. Issue 1's codes are read as Issue 1 defines them, because B.5 asks a client to read every version up to its own.

## What is not implemented

Every mandatory clause is met as we read it. A clause this directory does not meet is a skipped case whose message names the kind of gap (`firmware/test/adsl/README.md`), and `build/skyblip_tests --no-skip --test-case="ADS-L*"` prints them. Where the text does not settle a reading, `docs/ADSL_EASA_QUESTIONS.md` puts it to EASA, and a frozen case pins what the Go does. Payload 66 is held by `protocols/adsl_edge/` and its own cases.

A configured ICAO address goes on air under table 5 for every payload the unit sends (F.2.3, `core/settings/README.md`, The ICAO address). That no payload stays on the manufacturer's page is our reading of "thereby overriding the address given by the manufacturer", which EASA is asked to confirm. Remote Identification (G.5) is prose: it reserves a payload and leaves its format to a later issue.

The device does not take these optional clauses:

| Clause | What it is | Why it is optional | What our Status declares |
|---|---|---|---|
| C.3, C.3.1, C.3.2, D.1.2 | the O-band LDR channel at 38.4 kbps, its sync word, preamble and legacy packet | G.2.3 gives O-band LDR receive a None class | O-band LDR receive 0, None |
| E.4, E.4.1, E.4.2 | the Ed25519 secure signature and its timestamp | E.4: "ADS-L clients may optionally digitally sign the transmitted data packets" | no field. Every packet sets E.1.2's Secure Signature flag to 0 |
| F.2.4 | privacy mode, AMT 0 with an address drawn at start-up | F.2.4: "a device may be operated in privacy mode" | no field |
| G.4 | the FIS-B Uplink payload | G.2.10 lets a sender declare "Sender does not process FIS-B Uplink payloads" | FIS-B client 0 |

## Before you ship a copy

`encode_traffic()`, `encode_status()` and `Uplink::encode()` take the F.2.2 address table with no default, because it is a registry assignment, not a constant of the specification. The ADS-L registry assigned skyBlip table 58 with manufacturer prefix 0 (ticket #228794), and `core/settings/address.h` holds them. A product built from this code applies to `registry@ads-l.aero` for its own, and replaces the other identities `firmware/README.md` lists.

## The transmit rules

`transmit.h` holds the rules the specification sets for a transmitter, and leaves every choice inside them to the integrator:

- §C.5 gives ground stations 200 to 450 ms of each UTC second and aircraft 450 to 1000 (`kUplinkSlotStartMs`, `kUplinkSlotEndMs`, `kDirectStartMs`, `kDirectEndMs`). A burst starts and completes inside the direct slot.
- §C.2 puts a 24-byte frame on air for 4.64 ms, so `kAirTimeMs` books 5.
- §G.1.16 asks for at least 1 Hz airborne and 0.1 Hz on the ground (`report_period_s()`), and no navigation data older than 500 ms (`kFixMaxAgeMs`), from the PVT fix to the instant the burst goes on air, deferral included.
- §G.2.11 asks for a Status at least every 20 s (`kStatusPeriodS`).
- §C.2.5 asks consecutive Traffic transmissions, and consecutive Status transmissions, to change channel.

`core/timing/README.md` holds skyBlip's schedule, its retries and its readings of the ground rate and of the alternation.

## Media access

§D.3 asks for listen-before-talk "according to the Polite Spectrum Access rules laid out in ETSI EN 300 220-1", and adds two numbers. `media_access()` is the clause as a pure function of one channel assessment:

- A clear channel transmits.
- A busy one defers by a random whole number of units from the busy instant: at least EN 300 220-1's minimum deferral (`protocols/common/en300220.h`), at most the units the caller declares, never before the device can listen again, and never past the packet's window. With no unit left, the packet is missed.
- The wait stays inside the band's "Backoff Interval (see Subpart D)", `ChannelAttempt::backoff`: 15 to 250 ms on the M band (§C.2, the default) and 10 to 50 ms on the O band (§C.4). A caller whose units cannot reach the floor misses every busy packet.
- "If no packet can be transmitted 3000 ms after the initial attempt, the device may force transmission irrespective of carrier detect" is `kForceAfterAttemptUs`, applied only where the caller allows it.
- "After a forced transmission, the device may not transmit for at least 2000 ms" is `kSilenceAfterForceUs`, counted from the end of the forced transmission. An assessment inside it answers `Silent`, clear channel or not.

The integrator picks the CCA interval, the dead time, the threshold, the deferral unit, the draw, which packets may be forced and what to do with a missed one. skyBlip's answers are in `core/timing/README.md`.

## The Status payload

`encode_status()` writes §G.2's payload type 3, what a device can hear and process, under version 1, since the Status first appears in Issue 2. `receive_class()` maps the share of the direct slot heard to §G.2.2's classes: over 5% Occasional, over 40% Partial, over 80% Full. `core/protocol/README.md` says what skyBlip declares.

The F.2 header and G.2 Status payload together take 12 bytes. The network header and CRC bring the packet to 16 bytes, the length §D.1.1 requires. G.2 defines no padding, and those 12 bytes already meet F.2.5's alignment. The 24-byte device status in OGN Tracker is type 66 telemetry, not a padded G.2 Status. The receiver refuses a G.2 Status behind a clean length of 24, even when its first 16 bytes pass the CRC.

The length byte rides outside the CRC. The receiver checks only sizes that agree with every clean Manchester pair, and rejects lengths with more than two doubtful pairs. It can try both 16 and 24 only if bit 3 is doubtful, and refuses the burst if both sizes pass the CRC and payload checks. It checks at 24 bytes only when the chips after byte 16 were heard as a transmitter's.

If both chips in a length pair flip, the receiver sees a clean wrong bit and loses the packet. The old receiver could recover it by ignoring the length. With independent 1% chip errors in a host probe of one million bursts per payload, exact Traffic receptions rose from 895,650 to 904,588 and exact Status receptions rose from 964,998 to 974,821. A collision can flip both chips together, so the gain does not apply to all interference.

## Which issue a payload is

§E.1.1 asks every packet to carry the lowest protocol version whose encoding of its payload is the same: 0 for Issue 1, 1 for Issue 2. `lowest_version()` reads it off the clear packet, and the encoders set it. Issue 1 names only Traffic, so every other payload is version 1. The Traffic encoder uses version 0 unless the payload needs an Issue 2 encoding. These G.1 codes changed from [Issue 1](../../../docs/reference/ads-l_4_srd860_issue_1.md):

| Field | Code | Issue 1 | Issue 2 | Version |
|---|---|---|---|---|
| G.1.5 latitude, longitude | 0x800000 | outside the latitude or longitude bounds | no 2D fix | 1 |
| G.1.5 latitude, longitude | 0xFFFFFF | no 2D fix | one step south of the equator or west of Greenwich | 1 |
| G.1.7 altitude | 0x3FFF | 61112 m | invalid | 1 |
| G.1.7 altitude | 0x3FFE | 61104 m | 61104 m or more | 0 |
| G.1.8 ground speed | 0xFF | 238 m/s | invalid | 1 |
| G.1.8 ground speed | 0xFE | 236 m/s | 236 m/s or more | 0 |
| G.1.9 vertical rate | 0x1FF | -119 m/s | invalid | 1 |
| G.1.9 vertical rate | 0x1FE | -118 m/s | -118 m/s or less | 0 |
| G.1.9 vertical rate | 0x100 | absent climb, inferred from G.1 and G.1.6 | noncanonical zero, not the invalid marker | not sent |
| G.1.3 category | 3 | rotorcraft | light rotorcraft | 0 |
| G.1.3 category | 6 | ultralight | ultralight and motorized hang-glider | 0 |
| G.1.3 category | 7 | hang-glider or paraglider | paraglider | 0 |
| G.1.3 category | 14 to 17 | reserved | model plane, heavy rotorcraft, hang-glider, paramotor | 1 |
| G.1.15 GVA | 3 or 2 | 3 under 15 m, 2 from 15 to 45 m | 3 under 10 m, 2 from 10 to 45 m | 1 for a VFOM from 10 to 15 m (`Integrity::vertical_accuracy_issue2_only`), else 0 |

A code marked 0 still reads as true to an Issue 1 receiver, so it stays at version 0 for the widest reach. SIL, SDA, NIC, HFOM and the velocity accuracy keep their thresholds and codes. NIC's 13 to 15 are reserved in Issue 2, undefined in Issue 1, and never sent.

The low-level `set_*_invalid()` motion setters write Issue 2 markers but do not set the header. Use `encode_traffic()` to choose the version, or set it explicitly when building a packet by hand.

For direct Traffic, `decode_traffic()` uses the received version for the motion validity checks. It reads the three top motion codes as values in version 0, with the same exponential decoding as other values, and as unavailable in version 1. It reads 0x100 as absent climb in version 0 only ([question for EASA](../../../docs/ADSL_EASA_QUESTIONS.md)). For position, 0x800000 never gives a valid position, and 0xFFFFFF gives no fix in version 0 only. Category and accuracy codes are returned as received, without inferring a narrower category or an accuracy in metres.

## Soft versioning

§B.5 lets the specification move on without splitting the air. Every packet carries the version it is written in (§E.1.1), and the Status the highest version its sender reads (§G.2.1, `Packet::kMaxProtocolVersion`, 1 here). `status_max_version()` reads that field, and `traffic_max_version()` the version a Traffic payload came in, which §B.5 has a receiver assume until it hears a Status. Both return nothing for any other payload.

`close_client()` is §B.5's test for a neighbour own Traffic must stay readable to: under 1 km horizontally and 800 m vertically, or under 800 m vertically and under 30 s from the closest point of approach, the horizontal distance over the closure rate (`kCloseHorizontalM`, `kCloseVerticalM`, `kCloseTcpaS`). Own Traffic then goes out in the lowest version any close neighbour reads. The stack keeps no list of neighbours: which clients a device has heard, and for how long it believes them, is the integrator's (`core/traffic/README.md`, Which ADS-L version a client reads).

`issue1_form()` re-codes a Traffic payload in Issue 1's codes, as version 0. It answers false, leaving the packet as it was, for any other payload and wherever Issue 1 has no true code:

| Issue 2 code | Issue 1 form | Why |
|---|---|---|
| category 14, model plane | 0, no category information | Issue 1 has no code for it |
| category 15, heavy rotorcraft | 3, rotorcraft | Issue 1's rotorcraft covers every weight |
| category 16, hang-glider | 7, hang-glider or paraglider | Issue 2 split that class |
| category 17, paramotor | 7, hang-glider or paraglider | Issue 1's closest wing |
| position 0x800000, no fix | 0xFFFFFF | Issue 1's marker for no fix |
| a real position at 0xFFFFFF | 0xFFFFFE | one step on, inside any claimed accuracy, so Issue 1 does not read it as no fix |
| GVA 2 for a VFOM from 10 to 15 m | 3 | Issue 1 draws code 3 at 15 m. The caller passes the VFOM, since the code cannot tell |
| altitude, ground speed or vertical rate unknown | none: `issue1_form()` answers false | Issue 1 has no altitude or speed marker, and the transmit conversion does not use the inferred climb marker |

## The Traffic Uplink

`Uplink::encode()` builds G.3's payload type 4 for a ground station: its own address in the F.2 header, then for each target the 30-bit address of the sender and its 120-bit Traffic payload copied byte for byte, then the F.2.5 padding. The header says version 1, since Issue 1 has no uplink, and sets the error control bit, since the parity is E.3.2's at every size: E.3.2 recommends it past 64 bytes and allows it below. A ground station may send the CRC instead, because E.3.2 lets operators deviate and G.3 names the channel and the slot but no parity, so `Uplink::decode()` reads both. It counts the records from the D.1.1 length byte, as G.3 asks. F.2.5 pads the data to whole words, so a CRC packet's length is always a multiple of 4 and a FEC packet's always 1 past one: one to ten records are 28 to 200 bytes under the CRC and 57 to 229 under the FEC. The length alone names the parity (`uplink_layout()`). It checks that parity, E.3.1's CRC-24 or E.3.2's code, refuses a header whose error control bit names the other one, descrambles, and drops a record with no address or no position. `uplink_record_current()` is G.3's age rule: current up to 10 s after the instant its G.1.1 timestamp names, never before it, and never for a timestamp of 60 to 63, which "shall not be used". Ten records under the FEC make the longest packet, 229 bytes behind its length byte and 9.4 ms on air at C.4's 200 kbps (`kUplinkBurstUs`). The Go's O-band dwell reads the length byte and 229 bytes behind the sync word whatever the packet, and the length byte says how many of them are the packet.

An embedded Traffic record has no version field. The decoder uses `lowest_version()` to choose how to read it, so the three top motion codes select Issue 2 and read as unavailable. It cannot recover an original Issue 1 version from those codes. If no code selects Issue 2, a vertical rate of 0x100 reads as absent climb.

E.3.2 computes the parity over the network header and the scrambled data, prefixed with zeros to 223 bytes, and sends no padding. `ReedSolomon255` uses the field 1+x2+x3+x4+x8 (`kFieldPolynomial`, 0x11D), the primitive element alpha^11 in index form, as CCSDS 131.0 and Karn's fec library count it (`kPrimitiveElement`), and 32 roots from the 121st (`kFirstRoot`). Its names map to the textbook's:

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

`test/conformance/` holds the cases this component meets on its own, one file per subpart. `firmware/test/adsl/` holds the cases that need skyBlip around it, and its README says how both halves are named and checked.

Where the specification prints code or a worked example, the test carries it rather than restating our answer. E.2's XXTEA and E.3.1's CRC-24 are transcribed from the clause and compared against `protocols/common/`. `test/conformance/reference.h` lays out a Traffic Uplink from them and from E.3.2's three numbers, under either parity, so the component's G.3 receive cases decode packets `Uplink::encode()` never built. G.1.6's exponential encoder follows the algorithm as written. G.1.7 to G.1.9 assert the codes of the clauses' example tables, and the G.1 case reads every field at the payload table's bit offsets. Literal frames built from the two issues' tables exercise the different motion validity rules in the stack and through the Go's receiver. The E.3.2 case checks `ReedSolomon255` against a bit-by-bit encoder written from the clause's three numbers and against one block the reedsolo library encoded.
