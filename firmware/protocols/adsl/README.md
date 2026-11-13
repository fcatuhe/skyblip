# protocols/adsl

ADS-L 4 SRD-860 Issue 2, EASA's iConspicuity link, from the specification the declaration of conformity names: [`docs/reference/ads-l_4_srd860_issue_2.md`](../../../docs/reference/ads-l_4_srd860_issue_2.md), with the PDF beside it winning where the two disagree. `version.h` says so in `kSpecification`. Everything here is what the specification decides, and it builds and passes its tests with nothing but `protocols/common/` and doctest (`../README.md`). The ADS-L declaration of conformity rests on this directory: what goes on air and what is read off it is here, or in a skyBlip file `../README.md` lists.

## What is in

| File | Clauses | What it holds |
|---|---|---|
| `packet.{h,cpp}` | E, F.2, G.1, G.2 | `Packet`, the 24 bytes the CRC covers: the network header, the F.2 header and its sender address, the G.1 Traffic and G.2 Status fields at the offsets the tables print, XXTEA scrambling (E.2), the CRC-24 (E.3.1) and the erasure repair a receiver runs before it. `Traffic` and `encode_traffic()`, `decode_traffic()` are the Traffic payload as plain values. `StatusReport` and `encode_status()` are the Status payload. `timestamp_code()` is G.1.1, `hfom_cm()` is G.1.14's HFOM, and the NACp and GVA tables are G.1.14 and G.1.15. The F.2.2 address tables and G.1.4's "no emergency" are named constants |
| `physical.h` | C.2, C.4, D.1.1 | the two M-band channels and the O band, the M-band modulation and its 14 dBm e.r.p. ceiling, and the sync word with D.1.1's length byte behind it |
| `uplink.{h,cpp}`, `reed_solomon.{h,cpp}` | C.4, E.3.2, G.3 | the O-band HDR modulation, sync and burst, and an RS(255,223) code. The records are still skyBlip's own format, not G.3's |
| `flight_state.{h,cpp}` | G.1.2, G.1.16 | the flight state codes, the Undefined a craft able to hover announces on the ground, and the report period each state asks for |
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
- `RadioService` and the SX1262 settings that put a frame on air

## What is not implemented

Each gap is a skipped case in the conformance suite, written as the clause requires (below): O-band LDR (C.3, D.1.2), the secure signature (E.4), the Reed-Solomon roots and block prefix E.3.2 names, the verbatim Traffic payload in an uplink record and its age (G.3), FIS-B (G.4), Remote Identification (G.5), privacy mode (F.2.4) and soft versioning (B.5). `build/skyblip_tests --no-skip --test-case="ADS-L*"` prints them.

## Public API

Everything is in namespace `skyblip::adsl`.

- `kAddressTableRandom` to `kAddressTableManufacturerLast`, `manufacturer_page_table()`, `kEmergencyNone`, `kUereCm`, `hfom_cm()`
- `Packet` with `reset()`, the field accessors, `scramble()`, `descramble()`, `set_crc()`, `check_crc()` and `correct()`, and `kSyncWord`, `kFrameBytes`
- `Traffic`, `Integrity`, `encode_traffic()`, `decode_traffic()`, `timestamp_code()`
- `StatusReport`, `ReceiveClass`, `receive_class()`, `conspicuity::`, `encode_status()`
- `Uplink`, `UplinkTarget`, `encode_o_band()`, `ReedSolomon255`
- `FlightState`, `state_from()`, `announced_state()`, `reduced_rate()`, `report_period_s()`, `flies_at_no_ground_speed()`
- `on_schedule()`, `channel_in()`, `period_s()`, `kUplinkSlotStartMs`, `kUplinkSlotEndMs`, `kDirectStartMs`, `kDirectEndMs`, `kAirTimeMs`, `kFixMaxAgeMs`, `kStatusPeriodS`
- `kVersion`, `kSpecification`

## The transmit rules

`transmit.h` holds the rules the specification sets for a transmitter, and leaves every choice inside them to the integrator. §C.5 gives the ground stations 200 to 450 ms of each UTC second and the aircraft 450 to 1000 (`kUplinkSlotStartMs` to `kDirectEndMs`), and a burst starts and completes inside the direct slot. §C.2 puts a 24-byte frame on air for 4.64 ms, so `kAirTimeMs` books 5. §G.1.16 asks for at least 1 Hz airborne and 0.1 Hz on the ground (`report_period_s()`), and no navigation data older than 500 ms (`kFixMaxAgeMs`), measured to the top of the transmit second because the encoder carries the fix forward to the instant the burst names. §G.2.11 asks for a Status at least every 20 s (`kStatusPeriodS`).

`on_schedule()` says whether a second carries Traffic: every second airborne, and on the ground the one second of ten the integrator hands it, so the ground population spreads by the integrator's choice of second and a unit that reboots comes back to the same one. `channel_in()` is §C.2.5's alternation from one transmission to the next, counted by the clock, `(utc_s / period) & 1`: counting a device's own transmissions instead would put it out of step with the grid the first time anything refused a burst.

The rest is the integrator's, and skyBlip's choices are in `core/timing/README.md`: which second of ten a unit owns, the instant drawn inside the direct slot, where the Status goes, and the channel-access route a device declares in place of §D.3's listen-before-talk.

## The Status payload

`encode_status()` writes §G.2's payload type 3: what a device can hear and process, so a peer or a ground station can plan around it. Its network header says protocol version 1, because the Status payload first appears in Issue 2 and §E.1.1 numbers Issue 2 as 1. The Traffic payload keeps version 0, since Issue 1 encodes it the same way. `receive_class()` turns a share of the direct slot heard into §G.2.2's classes: more than 5% is Occasional, more than 40% Partial and more than 80% Full. What skyBlip declares in each field, and why, is in `core/protocol/README.md`.

The payload is 12 bytes, header included. The frame still carries 20 payload bytes, as a Traffic frame does, with the last 8 left zero, so it goes out behind the same sync word and the same length byte. §D.1.1 lets a receiver read the length, but a receiver that checks it as 0x18 in the sync tail, as skyBlip's does (`core/protocol/air.cpp`), would not hear a 16-byte Status frame, and the OGN code SoftRF carries builds every frame at 24 bytes (`libraries/OGN/ads-l.h:17-22`).

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

## The conformance suite

The specification is read clause by clause in two halves. `test/conformance/` holds the cases this component satisfies on its own, one file per subpart, and `firmware/test/adsl/` holds the cases that need skyBlip around it: the simulator, a product or the dwell map. `scripts/check_adsl_clauses.py` checks the two together: every `ADS-L.4.SRD860.x.y` heading in the specification has at least one case in one half, and every case names a real clause. `firmware/test/adsl/README.md` says how a case is named and what a skipped one means.

Where the specification prints code or a worked example, the test carries it rather than restating our answer: E.2's XXTEA and E.3.1's CRC-24 are transcribed from the clause and compared against `protocols/common/`, G.1.6's exponential encoder is implemented from the algorithm as written, and G.1.7 to G.1.9 assert the exact codes of the clauses' example tables. The G.1 case reads every field at the bit offsets of the payload table, so a field that moves by one bit fails there first.

The unit tests beside it take the packet over a bit-error channel and count detected failures apart from silent miscorrections (`test/test_packet.cpp`), pin a frame byte for byte, and check the uplink's code, the transmit rules and the hour's air time.
