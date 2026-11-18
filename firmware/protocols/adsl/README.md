# protocols/adsl

ADS-L 4 SRD-860 Issue 2, EASA's iConspicuity link, from the specification the declaration of conformity names: [`docs/reference/ads-l_4_srd860_issue_2.md`](../../../docs/reference/ads-l_4_srd860_issue_2.md), with the PDF beside it winning where the two disagree. `version.h` says so in `kSpecification`. Everything here is what the specification decides, and it builds and passes its tests with nothing but `protocols/common/` and doctest (`../README.md`). The ADS-L declaration of conformity rests on this directory: what goes on air and what is read off it is here, or in a skyBlip file `version.lock` freezes with it (`../README.md`, Versions).

## What is in

| File | Clauses | What it holds |
|---|---|---|
| `packet.{h,cpp}` | D.1.1, E, F.2, G.1, G.2 | `Packet`, the 24 bytes the CRC covers, 16 for a Status: the network header, the F.2 header and its sender address, the G.1 Traffic and G.2 Status fields at the offsets the tables print, XXTEA scrambling (E.2), the CRC-24 (E.3.1) and the erasure repair a receiver runs before it. `receive()` runs that repair on a frame off the air, then reads the E.1 network header the CRC covers: a header naming the FEC is `wire::Status::Invalid`, since an M-band frame is checked by its CRC, a version above `Packet::kMaxProtocolVersion` or key 1 or 2 is `Unsupported`, key 0 is descrambled and key 3, which E.1.3 lets a constrained sender use, is read in the clear. A packet whose length is not its payload type's, 16 for a Status and 24 otherwise, is `Invalid`. A frame no repair fits is `Crc`. `Traffic` and `encode_traffic()`, `decode_traffic()` are the Traffic payload as plain values. `StatusReport` and `encode_status()` are the Status payload. `timestamp_code()` is G.1.1, `hfom_cm()` is G.1.14's HFOM, and the NACp and GVA tables are G.1.14 and G.1.15. The F.2.2 address tables and G.1.4's "no emergency" are named constants |
| `physical.h` | C.2, C.4, D.1.1 | the two M-band channels and the O band, the M-band modulation and its 14 dBm e.r.p. ceiling, each band's backoff interval (`kMBandBackoff`, 15 to 250 ms, and `kOBandBackoff`, 10 to 50 ms), and the sync word with D.1.1's length byte behind it |
| `uplink.{h,cpp}`, `reed_solomon.{h,cpp}` | C.4, D.1.1, E.3.2, G.3 | the O-band HDR modulation, sync and burst, the G.3 Traffic Uplink packet with up to ten verbatim Traffic payloads and its 10 s age rule, and E.3.2's RS(255,223) code with its roots and zero prefix |
| `flight_state.{h,cpp}` | G.1.2, G.1.16 | the flight state codes and the report period each state asks for |
| `soft_versioning.{h,cpp}` | B.5, G.2.1 | the Max. Protocol Version a packet declares, the test that makes a client close enough to write for, and `issue1_form()`, a Traffic payload re-coded in Issue 1's codes |
| `transmit.h` | C.2, C.2.5, C.5, G.1.16, G.2.11 | the rules a transmitter keeps: the uplink and direct slots, a frame's air time, the airborne and ground report periods, the 500 ms nav age and the Status period |
| `media_access.{h,cpp}` | D.3 | `media_access()`, the media access rule: a clear channel transmits, a busy one defers by a random whole number of units inside its band's backoff interval, never more than the caller declares and never past the packet's window, is missed past them, may be forced 3000 ms after the initial attempt, and silences the device for 2000 ms after a forced transmission ends. The deferral floor is EN 300 220-1's, from `protocols/common/en300220.h` |
| `version.h` | | `kVersion` and `kSpecification` |

## What is out, and where it is

These are skyBlip's choices, built on this stack:

- which aircraft categories announce G.1.2's Undefined rather than on ground, because no ground speed shows they have landed (`core/flight/state.*`, `announced_state()`)
- own-ship as a Traffic payload: the extrapolation to the instant the timestamp names, which fields claim invalid without a fix, the VFOM taken from VDOP by G.1.14's model, and the velocity accuracy left at 0, unknown, because the receiver reports no velocity error (`core/protocol/adsl.*`)
- which second of ten a unit owns, the instant drawn inside the direct slot, where the Status goes, and the hour's air time declared as the channel-access route (`core/timing/transmit.*`, `core/timing/channel.*`)
- the listen-before-talk §D.3 asks for, built on `media_access()`: the EN 300 220-1 parameters skyBlip declares, the deferral unit and its draw, the CCA threshold at this antenna (`core/timing/channel.*`), which packets it forces, what counts as the initial attempt, and how a deferred burst is re-armed (`products/skyblip_go/services/radio_access.cpp`), and the assessment in the executor (`hardware/platform/zephyr/rf.h`)
- the address table 58 and the manufacturer prefix, and F.2.3's ICAO address under table 5 in their place while the pilot has set one (`core/settings/address.h`, `on_air()`), what the Go refuses as an inconsistent configuration (`products/skyblip_go/settings.*`), and airborne detection (`core/flight/state.*`)
- the single-radio dwell map, which decides when each channel is heard and so the M-band receive class the Status declares (`core/timing/slot.*`)
- the dual-system receiver that names ADS-L or ALP-TAS from one shared sync window (`core/protocol/air.*`)
- B.5's bookkeeping: each target's Max. Protocol Version and the lowest any close one reads (`core/traffic/table.*`), when own Traffic goes in its Issue 1 form, and that a position with no Issue 1 form is held (`products/skyblip_go/services/radio_frames.cpp`)
- `RadioService` and the SX1262 settings that put a frame on air

## What the receiver refuses

The stack reads what Issue 2 defines and nothing looser, so another sender is heard only when it follows the specification too. `receive()` refuses a frame no repair fits, a header naming the FEC on a CRC frame, a version above 1, key 1 or 2, and a packet whose length is not its payload type's. `receive_as_heard()` answers for one length, never for both. `Uplink::decode()` refuses a length no whole number of records spells, a header without the FEC, a version above 1, key 1 or 2, and a payload that is not type 4. `uplink_record_current()` refuses a timestamp of 60 to 63. Issue 1's codes are read as Issue 1 defines them, because B.5 asks a client to read every version up to its own.

## What is not implemented

Each clause this directory does not meet is a skipped case in the conformance suite, written as the clause requires, and the message says which kind of gap it is (`firmware/test/adsl/README.md`). `build/skyblip_tests --no-skip --test-case="ADS-L*"` prints them.

Every mandatory clause is met as we read it. Where the text does not settle a reading, `docs/ADSL_EASA_QUESTIONS.md` puts it to EASA, and a frozen case pins what the Go does, but for payload 66, which `protocols/adsl_edge/` and its own cases hold. §F.2.3 was the last clause met: a configured ICAO address goes on air under table 5, for every payload the unit sends, and the Go refuses the configurations it can know to be inconsistent (`core/settings/README.md`, The ICAO address). That every payload changes table, and none stays on the manufacturer's page, is our reading of "thereby overriding the address given by the manufacturer", and EASA is being asked to confirm it. Remote Identification (G.5) is prose: it reserves a payload for drones and leaves its format to a later issue.

The rest are optional, and this device does not take them:

| Clause | What it is | The wording that makes it optional | What our Status declares |
|---|---|---|---|
| C.3, C.3.1, C.3.2, D.1.2 | the O-band LDR channel at 38.4 kbps, its sync word, preamble and legacy packet | G.2.3 gives O-band LDR receive a None class, so a device can declare the channel absent | O-band LDR receive 0, None |
| E.4, E.4.1, E.4.2 | the Ed25519 secure signature and its timestamp | E.4: "ADS-L clients may optionally digitally sign the transmitted data packets". E.1 calls it "the optional secure signature" | no Status field. Every packet sets E.1.2's Secure Signature flag to 0 |
| F.2.4 | privacy mode, AMT 0 with an address drawn at start-up | F.2.4: "a device may be operated in privacy mode" | no Status field |
| G.4 | the FIS-B Uplink payload, DO-358B's ground uplink message | G.2.10 lets a sender declare "Sender does not process FIS-B Uplink payloads" | FIS-B client 0 |

## Before you ship a copy

Nothing in this directory names skyBlip, and nothing in it picks an address table for you. `encode_traffic()`, `encode_status()` and `Uplink::encode()` take the F.2.2 table as an argument with no default, because it is a registry assignment and not a constant of the specification. skyBlip transmits under table 58 with manufacturer prefix 0, which the ADS-L registry assigned to skyBlip (ticket #228794), or under table 5 with the aircraft's ICAO address once a pilot sets one (F.2.3), and keeps both in `core/settings/address.h`. A product built from this code applies to the registry (`registry@ads-l.aero`) for its own. The other identities a copy has to replace are listed in `firmware/README.md`.

## Public API

Everything is in namespace `skyblip::adsl`.

- `kAddressTableRandom` to `kAddressTableManufacturerLast`, `manufacturer_page_table()`, `kEmergencyNone`, `kUereCm`, `hfom_cm()`
- `Packet` with `reset()`, the field accessors, `key_index()`, `data_bytes()`, `kStatusDataBytes`, `scramble()`, `descramble()`, `set_crc()`, `crc_ok()` and `correct()`, the free `receive()` and `receive_as_heard()`, and `kSyncWord`, `sync_word()`, `kFrameBytes`
- `Traffic`, `Integrity`, `encode_traffic()`, `decode_traffic()`, `timestamp_code()`, `lowest_version()`, `issue2_category()`
- `StatusReport`, `ReceiveClass`, `receive_class()`, `conspicuity::`, `encode_status()`
- `Uplink`, `UplinkStatistics`, `uplink_length()`, `uplink_payload_bytes()`, `uplink_record_current()`, `encode_o_band()`, `ReedSolomon255`
- `status_max_version()`, `traffic_max_version()`, `close_client()`, `issue1_form()`, `kCloseHorizontalM`, `kCloseVerticalM`, `kCloseTcpaS`, `Packet::kMaxProtocolVersion`
- `FlightState`, `state_from()`, `reduced_rate()`, `report_period_s()`
- `period_s()`, `kUplinkSlotStartMs`, `kUplinkSlotEndMs`, `kDirectStartMs`, `kDirectEndMs`, `kAirTimeMs`, `kFixMaxAgeMs`, `kStatusPeriodS`
- `media_access()`, `ChannelAttempt`, `Deferral`, `AccessVerdict`, `Access`, `kForceAfterAttemptUs`, `kSilenceAfterForceUs`, `Backoff`, `kMBandBackoff`, `kOBandBackoff`
- `kVersion`, `kSpecification`

## The transmit rules

`transmit.h` holds the rules the specification sets for a transmitter, and leaves every choice inside them to the integrator. §C.5 gives the ground stations 200 to 450 ms of each UTC second and the aircraft 450 to 1000 (`kUplinkSlotStartMs` to `kDirectEndMs`), and a burst starts and completes inside the direct slot. §C.2 puts a 24-byte frame on air for 4.64 ms, so `kAirTimeMs` books 5. §G.1.16 asks for at least 1 Hz airborne and 0.1 Hz on the ground (`report_period_s()`), and no navigation data older than 500 ms (`kFixMaxAgeMs`). §G.2.11 asks for a Status at least every 20 s (`kStatusPeriodS`).

§G.1.16's ground rate is "0.1 Hz (once every 10 seconds)". skyBlip reads that as an average: its ground positions go 9 then 11 s apart, exactly 0.1 Hz on average, because §C.2.5's alternation with a channel picked by the second's parity cannot put two positions 10 s apart. §C.2.5 asks consecutive Traffic transmissions, and consecutive Status transmissions, to change channel, which the stack leaves to the integrator to schedule.

§G.1.16's 500 ms is skyBlip's reading too, made for a receiver that solves once a second while the direct slot runs from 450 to 1000 ms. The fix may be at most 500 ms old at the top of the transmit second, and the encoder carries it forward to the instant the burst names, so the G.1.1 timestamp names that instant and the position belongs to it. Read literally, as the age of the measurement when the burst leaves, the rule would need a receiver solving at 5 Hz.

Which seconds carry which burst, on which channel and at which instant is the integrator's, and skyBlip's choices are in `core/timing/README.md`. The second alone picks the channel, A in an even UTC second and B in an odd one for a position and the other way round for the Status, inside a 20 s cycle per unit that starts on an even second, with ground positions 9 then 11 s apart. A burst that did not go out in its second, for any reason, a §B.5 hold among them, is retried 2 s later on the same channel, or at the next same-parity second whose dwell on that channel is free of ours, until it goes out or its type's next turn is due. skyBlip reads §C.2.5 so: every transmission the schedule makes alternates, and a burst that never went out does not move the next one's channel.

## Media access

§D.3 makes the MAC CSMA, with listen-before-talk "according to the Polite Spectrum Access rules laid out in ETSI EN 300 220-1", and adds two numbers of its own. `media_access()` is the clause as a pure function of one assessment: whether the channel was clear, when, when the packet was first attempted, when the device last forced one, the earliest it can listen again and the last instant the packet may start. A clear channel transmits. A busy one defers by a random whole number of units from the busy instant, at least EN 300 220-1's minimum deferral period and at most the units the caller declares, never before the device can listen again and never past the window. The wait also stays inside the "Backoff Interval (see Subpart D)" row of its band's table, `ChannelAttempt::backoff`: 15 to 250 ms on the M band (§C.2), the default, and 10 to 50 ms on the O band (§C.4). A caller whose units cannot reach the band's floor misses every busy packet, so an integrator declares at least that much. It misses the packet when no unit is left, so a caller that hears late draws from what is left and never waits longer than it declared. "If no packet can be transmitted 3000 ms after the initial attempt, the device may force transmission irrespective of carrier detect" is `kForceAfterAttemptUs`, applied only where the caller allows a force, since the clause says "may". "After a forced transmission, the device may not transmit for at least 2000 ms" is `kSilenceAfterForceUs`, counted from the end of the forced transmission: an assessment inside it answers `Silent`, clear channel or not.

What the stack leaves to the integrator: the CCA interval, the dead time, the threshold at its own antenna, the deferral unit and how many of them a draw spans, the random draw itself, which packets may be forced, and what to do with a missed one. skyBlip's answers are in `core/timing/README.md`.

## The Status payload

`encode_status()` writes §G.2's payload type 3: what a device can hear and process, so a peer or a ground station can plan around it. Its network header says protocol version 1, because the Status payload first appears in Issue 2 (below). `receive_class()` turns a share of the direct slot heard into §G.2.2's classes: more than 5% is Occasional, more than 40% Partial and more than 80% Full. What skyBlip declares in each field, and why, is in `core/protocol/README.md`.

The payload is 12 bytes, header included, so the packet is 16 bytes with the network header and the CRC, and its §D.1.1 length byte says 16 (`Packet::kStatusDataBytes`, `data_bytes()`). `scramble()`, `set_crc()` and `receive()` take the length, so three words are scrambled and the CRC follows them, and `sync_word()` puts the length behind §C.2.4's sync word. `receive()` takes a length and refuses a packet whose payload type wants another, so a Status padded to Traffic's 24 bytes, or a Traffic cut to 16, is `Invalid`. The length byte rides in the receiver's sync tail, outside the CRC, and 16 and 24 differ in one bit of it, so one misheard bit would cost the frame. `receive_as_heard()` takes the length byte as the receiver heard it and reads the frame at both lengths. It keeps the one reading that comes out `Ok`, and refuses a frame both readings accept as `Invalid`, since no byte under the CRC says which length was sent: a Status followed by 0x00 chips is one, because zeros behind a clean CRC codeword keep it clean. It reads 24 bytes only where the chips past the 16th byte were heard as a transmitter's, so a Status followed by silence reads at 16 alone.

## Which issue a payload is

§E.1.1 asks every packet to carry the lowest protocol version whose encoding of its payload is the same, Issue 1 being 0 and Issue 2 being 1. `lowest_version()` reads it off the clear packet, and `encode_traffic()` and `encode_status()` set it. Issue 1's payload table names only Traffic, type 2, so every other payload is version 1: the Status, the Traffic Uplink and every type Issue 1 reserves. A Traffic payload is version 0 unless it carries a code whose meaning Issue 2 changed. These are every code of G.1 that changed between `docs/reference/ads-l_4_srd860_issue_1.md` and Issue 2:

| Field | Code | Issue 1 | Issue 2 | Version |
|---|---|---|---|---|
| G.1.5 latitude, longitude | 0x800000 | a position, 90 S or 180 W | no 2D fix (Issue 1 marked that 0xFFFFFF) | 1 |
| G.1.5 latitude, longitude | 0xFFFFFF | no 2D fix | a position one step south of the equator or west of Greenwich | 1. A version 0 packet with 0xFFFFFF in either field is Issue 1's no fix, and `has_position()` reads it so |
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
| G.1.15 GVA | 3 or 2 | 3 for a VFOM under 15 m | 3 under 10 m, 2 from 10 to 45 m | 1 for a VFOM from 10 to 15 m, which the two issues code differently (`Integrity::vertical_accuracy_issue2_only`). 0 otherwise |

Every other G.1 table only writes its bounds out: SIL, SDA, NIC, HFOM and the velocity accuracy keep the same thresholds and the same codes, Issue 2 writing each row as a range where Issue 1 wrote its upper bound, and NIC's 13 to 15, reserved in Issue 2, are codes Issue 1 never defined and this device never sends. G.1.10, G.1.16 and G.1.17 add rules and no codes. A code marked 0 in the last column is one an Issue 1 receiver still reads as a true statement, so it stays at version 0 for the widest reach. The clause cases in `test/conformance/test_network.cpp`, `test_traffic.cpp` and `test_quality.cpp` pin one row each.

## Soft versioning

§B.5 lets the specification move on without splitting the air. Every packet carries the version it is written in (§E.1.1, above), and the Status carries the highest version its sender reads (§G.2.1, `Packet::kMaxProtocolVersion`, 1 here). `status_max_version()` reads the Status field, and `traffic_max_version()` the version a Traffic payload came in, which §B.5 tells a receiver to assume until it hears a Status. Each returns nothing for any other payload, since §B.5 names only those two. `close_client()` is §B.5's test for a neighbour own Traffic has to stay readable to: under 1 km horizontally and 800 m vertically, or under 800 m vertically and less than 30 s from the closest point of approach, that time being the horizontal distance over the closure rate. A Traffic packet then goes out in the lowest version any close neighbour reads. Which targets are tracked is skyBlip's, in `core/traffic/README.md`.

`issue1_form()` writes a Traffic payload in Issue 1's codes, as version 0, for a close neighbour that reads no later issue. It re-codes a field only where Issue 1 has a code that is still true, and answers false, leaving the packet as it was, where it has none. It answers false for any payload but Traffic, since §B.5 asks this of Traffic only.

| Issue 2 code | In the Issue 1 form | Why |
|---|---|---|
| category 14, model plane | 0, no category information | Issue 1 has no code for it |
| category 15, heavy rotorcraft | 3, rotorcraft | Issue 1's rotorcraft covers every weight |
| category 16, hang-glider | 7, hang-glider or paraglider | Issue 2 split that class |
| category 17, paramotor | 7, hang-glider or paraglider | a powered paraglider, and Issue 1's closest wing |
| position 0x800000, no fix | 0xFFFFFF | Issue 1's own marker for no fix |
| a real position at 0xFFFFFF, one step south or west of zero | 0xFFFFFE | one step on, well inside any accuracy a device claims, so Issue 1 does not read it as no fix |
| GVA 2 for a VFOM from 10 to 15 m | 3 | Issue 1 G.1.15 draws code 3 at 15 m, Issue 2 at 10 m. The caller passes the VFOM, since the code alone cannot tell |
| altitude, ground speed or vertical rate unknown | no Issue 1 form: `issue1_form()` answers false | Issue 1 has no code for unknown, and 0x3FFF, 0xFF or 0x1FF would read as 61112 m, 238 m/s or a 119 m/s descent |

## The Traffic Uplink

`Uplink::encode()` builds G.3's payload type 4 for a ground station: the station's own address in the F.2 header, then for each target the 30-bit address of the aircraft that sent it and its 120-bit Traffic payload copied byte for byte, then the padding F.2.5 asks for. The network header says version 1, because Issue 1 has no uplink, and sets the error control bit, because the parity is E.3.2's and not the CRC. `Uplink::decode()` counts the records from the D.1.1 length byte, as G.3 asks, refuses a length no whole number of records can spell, and refuses a record with no address or no position before it becomes a target. `uplink_record_current()` is G.3's age rule: a record is current up to 10 s after the instant its G.1.1 timestamp names, and never before it. A timestamp of 60 to 63, which G.1.1 says "shall not be used", names no instant, so its record is never current. The longest packet, ten records, is 229 bytes behind its length byte and takes 9.4 ms of air at C.4's 200 kbps (`kUplinkBurstUs`).

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

The unit tests beside it take the packet over a bit-error channel and count detected failures apart from silent miscorrections (`test/test_packet.cpp`), pin a frame byte for byte, and check the uplink's code, the transmit rules, §D.3's media access branch by branch, and the hour's air time.
