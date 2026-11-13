# protocols/adsl

ADS-L 4 SRD-860 Issue 2, EASA's iConspicuity link, from the specification the declaration of conformity names: [`docs/reference/ads-l_4_srd860_issue_2.md`](../../../docs/reference/ads-l_4_srd860_issue_2.md), with the PDF beside it winning where the two disagree. `version.h` says so in `kSpecification`. Everything here is what the specification decides, and it builds and passes its tests with nothing but `protocols/common/` and doctest (`../README.md`). The ADS-L declaration of conformity rests on this directory: what goes on air and what is read off it is here, or in a skyBlip file `../README.md` lists.

## What is in

| File | Clauses | What it holds |
|---|---|---|
| `packet.{h,cpp}` | E, F.2, G.1, G.2 | `Packet`, the 24 bytes the CRC covers: the network header, the F.2 header and its sender address, the G.1 Traffic and G.2 Status fields at the offsets the tables print, XXTEA scrambling (E.2), the CRC-24 (E.3.1) and the erasure repair a receiver runs before it. `Traffic` and `encode_traffic()`, `decode_traffic()` are the Traffic payload as plain values. `StatusReport` and `encode_status()` are the Status payload. `timestamp_code()` is G.1.1, and the accuracy codes are G.1.14 to G.1.16 |
| `physical.h` | C.2, C.4, D.1.1 | the two M-band channels and the O band, the M-band modulation and its 14 dBm e.r.p. ceiling, and the sync word with D.1.1's length byte behind it |
| `uplink.{h,cpp}`, `reed_solomon.{h,cpp}` | C.4, E.3.2, G.3 | the Traffic Uplink: the O-band HDR modulation, sync and burst, the RS(255,223) code and the records, as `UplinkTarget` |
| `flight_state.{h,cpp}` | G.1.2, G.1.16 | the flight state codes, the Undefined a craft able to hover announces on the ground, and the report period each state asks for |
| `transmit.{h,cpp}` | C.2.5, C.5, D.3, G.1.16, G.2.11 | the transmit timing ADS-L defines: the direct slot, the schedule, channel alternation for Traffic and Status, the 500 ms nav age, the Status cadence, the instants drawn inside a window, and `AirTime`, the hour's air time declared as the channel-access route |
| `version.h` | | `kVersion` and `kSpecification` |

## What is out, and where it is

These are skyBlip's choices, built on this stack:

- own-ship as a Traffic payload: the extrapolation to the instant the timestamp names, which fields claim invalid without a fix, and the accuracy claimed from the receiver's DOP (`core/protocol/adsl.*`)
- the address table 58 and the manufacturer prefix (`core/settings/address.h`), and airborne detection (`core/flight/state.*`)
- the single-radio dwell map, which decides when each channel is heard and so the M-band receive class the Status declares (`core/timing/slot.*`)
- the callsign, OGN's payload 66 that F.2.1 leaves to OGN: its record, its window in slot 1's tail and its second (`core/protocol/callsign.*`, `core/timing/transmit.*`)
- the dual-system receiver that names ADS-L or ALP-TAS from one shared sync window (`core/protocol/air.*`)
- `RadioService` and the SX1262 settings that put a frame on air

## What is not implemented

Each gap is a skipped case in the conformance suite, written as the clause requires (below): O-band LDR (C.3, D.1.2), the secure signature (E.4), the Reed-Solomon roots and block prefix E.3.2 names, the verbatim Traffic payload in an uplink record and its age (G.3), FIS-B (G.4), Remote Identification (G.5), privacy mode (F.2.4) and soft versioning (B.5). `build/skyblip_tests --no-skip --test-case="ADS-L*"` prints them.

## Public API

Everything is in namespace `skyblip::adsl`.

- `Packet` with `reset()`, the field accessors, `scramble()`, `descramble()`, `set_crc()`, `check_crc()` and `correct()`, and `kSyncWord`, `kFrameBytes`
- `Traffic`, `Integrity`, `encode_traffic()`, `decode_traffic()`, `timestamp_code()`
- `StatusReport`, `ReceiveClass`, `receive_class()`, `conspicuity::`, `encode_status()`
- `Uplink`, `UplinkTarget`, `encode_o_band()`, `ReedSolomon255`
- `FlightState`, `state_from()`, `announced_state()`, `reduced_rate()`, `report_period_s()`, `flies_at_no_ground_speed()`
- `ground_second()`, `on_schedule()`, `channel_in()`, `status_due()`, `status_channel_in()`, `instant_between()`, `status_instant_between()`, `AirTime`, `kDirectStartMs`, `kDirectEndMs`, `kAirTimeMs`, `kFixMaxAgeMs`, `kStatusPeriodS`
- `kVersion`, `kSpecification`

## The burst is placed, and nothing on air moves it

`instant_between()` draws the instant from `hash(address ^ hash(utc_s))`, uniform over the window it is given, re-keyed every second. skyBlip gives it the direct slot clipped to the dwell that carries the burst (`core/timing/README.md`), and its executor keys the PA there. It does not listen first, it does not back off, it does not defer to anything it receives: `RfPlan::tx_at_us` is the whole contract, and the only rule that can still refuse a burst is the hour's air time (`AirTime`, below).

This is a deliberate deviation from a plain reading of ADS-L 4 SRD-860 issue 2 §D.3, which describes CSMA with listen-before-talk. Carrier sense cannot do the job the clause implies here, for four reasons.

It cannot see the burst it would collide with. A burst is 5 ms and the direct slot 550 ms wide, so two aircraft collide when their drawn instants land within 5 ms of each other: about 1.8% per target in range. Half of those we drew the earlier instant for, and an assessment only ever detects a burst *already* on air. The theoretical ceiling against our own kind is around 0.9% of bursts saved per neighbour.

It listens in the wrong place. The collision happens at the receiver's antenna and the assessment is made at ours. A third aircraft the other side of us hears both bursts collide while we heard nothing, and we defer to a station the intended receiver cannot hear at all. At these ranges the hidden terminal is the normal case, not the corner one.

It is weighted backwards. A burst has to clear the floor by a good margin to register at all, so the traffic we would successfully defer to is the near, loud traffic whose position we already have, while the distant aircraft near the noise floor, the one whose update matters, is never heard and never deferred to.

And it costs the one thing this device exists for. An aircraft that goes quiet is invisible. What protects the band is the randomised instant, decorrelated by device address and redrawn every second so a collision is not repeated, and the 1% duty cycle declared as the channel-access route (below).

None of the references cancel a burst either. `pjalocha/nrf52-ogn-tracker` (`src/ogn-radio.cpp:840-851`) backs off and escalates, then falls through and transmits when the slot runs out; its loop has no path that drops the packet. `pjalocha/esp32-ogn-tracker` has the code but both `TimeSlot()` call sites pass `MaxWait=0`, which skips it entirely. Neither SoftRF fork has listen-before-talk at all. A device built on this stack is the same on air as those, with one fewer moving part.

## The schedule is UTC's, not the transmitter's

Airborne, own-ship speaks in every second. On the ground §G.1.16 asks for 0.1 Hz, and `on_schedule()` makes that one second of UTC's own ten: `utc_s % 10 == hash(address) % 10`, the address's `ground_second()`. The second is the address's, so the ground population spreads evenly over the ten by construction, and a device that reboots or loses its fix for a minute comes back to the same one rather than to wherever the restart left it.

It used to be a gate: ten seconds since the last completed burst, then wait for the alternating channel's dwell to come round, then draw an instant inside it. That reads 10.2 to 12.5 seconds on a bench, which is under 0.1 Hz whichever way the clause is read, and the interval carried the previous burst's jitter into the next one for ever.

`channel_in()` is the same argument one level down. §C.2.5 alternates the two M-band channels from one transmission to the next, and the clock counts them: `(utc_s / period) & 1`. Counting our own transmissions instead put the alternation permanently out of step with the grid the first time anything refused a burst, and nothing on the device could see that it had.

What is not the clock's is the instant inside the slot: that stays a fresh draw per second, which is what decorrelates two devices that share a second (above). The 500 ms nav age of §G.1.16 is `kFixMaxAgeMs`, measured to the top of the transmit second, because the encoder carries the fix forward to the instant the burst names.

## The duty cycle is the channel-access route

EN 300 220-2 V3.3.1 Table 4 band M is 1% of any hour, and that limit is the channel-access route a device built on this stack declares in place of §D.3's listen-before-talk. `AirTime` is the evidence: sixty one-minute buckets on a ring that turns by elapsed time, so the hour that straddles `milliseconds()`'s 49.7-day wrap is an hour like any other.

One 5 ms Traffic burst a second and a Status every twenty seconds spend 0.525 of the allowance, so an empty budget can only be a fault. It blocks: a faulted transmitter that will not stop is worse for everyone on the band than a quiet one. skyBlip adds its callsign to that, 0.575 in all, and reports a refusal as `Attempt::over_budget` (`core/timing/README.md`).

## The Status payload and its seconds

`encode_status()` writes §G.2's payload type 3: what a device can hear and process, so a peer or a ground station can plan around it. Its network header says protocol version 1, because the Status payload first appears in Issue 2 and §E.1.1 numbers Issue 2 as 1. The Traffic payload keeps version 0, since Issue 1 encodes it the same way. `receive_class()` turns a share of the direct slot heard into §G.2.2's classes: more than 5% is Occasional, more than 40% Partial and more than 80% Full. What skyBlip declares in each field, and why, is in `core/protocol/README.md`.

The payload is 12 bytes, header included. The frame still carries 20 payload bytes, as a Traffic frame does, with the last 8 left zero, so it goes out behind the same sync word and the same length byte. §D.1.1 lets a receiver read the length, but a receiver that checks it as 0x18 in the sync tail, as skyBlip's does (`core/protocol/air.cpp`), would not hear a 16-byte Status frame, and the OGN code SoftRF carries builds every frame at 24 bytes (`libraries/OGN/ads-l.h:17-22`).

§G.2.11 asks for a Status at least every 20 s, and `kStatusPeriodS` is exactly 20. `status_due()` puts it in the address's ground second every other time that second comes round, so on the ground it rides beside a Traffic burst. §C.2.5 makes Status alternate channels just as Traffic does, counted on its own transmissions, so `status_channel_in()` is `(utc_s / kStatusPeriodS) & 1`, the same clock count `channel_in()` uses for Traffic. A Status refused once leaves the next one on the channel the last one used, and a 40-second gap: §G.2.11's twenty seconds is the nominal rate, which is how the clause words it.

`status_instant_between()` draws the Status instant from `hash(hash(address ^ hash(utc_s)))`, one more round of the hash the Traffic instant is drawn from, so the two are as unrelated as two devices' instants are. It is drawn over the same window as the Traffic instant, less the instants that would start within `apart_ms` of it, so two bursts that share a channel never overlap. The window is the same whether or not Traffic goes out that second, so the draw stays a pure function of the address and the second. skyBlip passes the burst's air time and its radio's completion guard as `apart_ms`.

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
