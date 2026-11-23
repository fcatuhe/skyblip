# core/radio

The station log: every burst this radio sent or heard, newest first.

Totals such as `rx_ok` cannot tell an empty sky from a receiver that frames nothing, and the traffic table holds only what decoded. The log also keeps the bursts that arrived and did not become a frame, which tell "nobody is transmitting" from "I am deaf to everybody".

`Verdict` names the sixteen things that can happen to a burst.

| | |
|---|---|
| `Transmitted` | own-ship's burst left the antenna |
| `Lost` | own-ship's burst was armed and never completed: the dwell ran out, or the radio's transmit timeout did |
| `Held` | the hour's air-time budget refused it (`timing::Transmitter::Attempt::over_budget`) |
| `Unarmed` | `ports::Rf` refused the plan that carried it, so nothing was armed |
| `Received` | a burst arrived, framed, and named an aircraft |
| `Named` | a burst framed and carried an aircraft's registration instead of its position: §F.2.1's payload 66 with OGN's info header, filed under the sender's address (`core/traffic/callsigns.h`) |
| `BadCrc` | an integrity check refused it: the chip's own CRC, an ADS-L or ALP-TAS CRC no forward correction could rescue, or a Reed-Solomon codeword the uplink could not correct |
| `Unframed` | clean chips behind the sync window and a sync tail naming neither system, so nothing was attempted |
| `Miskeyed` | an ALP-TAS frame whose CRC held and whose sender keyed it on a second inside `kKeyWindowS` of ours, but not ours |
| `Undecoded` | the frame was attempted and refused: the ALP-TAS plausibility gate with no second that would have read it, an ADS-L frame carrying no position, or a registration whose text is not a callsign (`protocol::is_callsign_character`) |
| `Unsupported` | a message type this firmware does not read. ALP-TAS reads only its 2024 position frame, so Air V6 (type 0) and text (type 3) land here. ADS-L reads §F.2.1's Traffic, the Status and the registration inside payload 66, so a relayed Status (3), our own Status heard back, Remote Identification (6) and other telemetry in 66 land here |
| `Unattempted` | own-ship had no fix or no UTC to decode an ALP-TAS frame against |
| `Declared` | an ADS-L Status its sender keyed itself, whose Max. Protocol Version B.5 files for it whether or not the aircraft is on the display, unless the table is full of close clients (`core/traffic/README.md`, Which ADS-L version a client reads), printed `STAT` |
| `Deferred` | the clear channel assessment found the channel busy, and the burst is armed again a random 15 to 250 ms later, as far as its window reaches (ADS-L C.2 and D.3), printed `DEFER`. The row is dated at the planned instant and carries the level the assessment read |
| `Forced` | on the ground, the channel was still busy 3000 ms after the first attempt, so the burst goes irrespective of carrier detect and nothing of ours follows for 2000 ms after it ends, printed `FORCE` |
| `Busy` | the burst was found busy and could not be armed again inside its window, so the 2 s retry takes it, printed `BUSY` |

`BadCrc` is a radio problem, answered by moving the antenna. `Undecoded` is ours, answered by reading `core/protocol/`. It is the only refusal that means this firmware may be wrong: `Unattempted`, `Unframed`, `Miskeyed` and `Unsupported` are not.

`Unattempted` exists because an ALP-TAS position is coded relative to the receiver and its key stage comes from the UTC second, so `TrafficService::decode_alptas` cannot start without a fix and a date. ADS-L needs no such gate, because §G.1 carries an absolute position.

A SoftRF built with `USE_INTERLEAVING` sends Air V6 between its Air V7 frames, so it shows as a stream of `Unsupported` rows.

`Miskeyed` diagnoses a clock disagreement. The ALP-TAS key stage moves once every 16 seconds (`utc_s >> 4`) and the frame carries the low 4 bits of its sender's second, so the decode tolerates one second of disagreement. Beyond that every frame from the target fails, with no other symptom. `alptas::keyed_second` finds the second that decrypts into a structured frame, and the row prints the offset as `KEY+18` or `KEY-2`. The frame is still refused.

The ALP-TAS address word is in the plaintext first two words under the frame CRC, so `TYPE`, `KEY` and `DEC` rows carry the sender's address (`Entry::address_valid`).

A `CRC` row carries the byte count and the level, because `GetPacketStatus` survives the failure: -110 dBm is a burst at the floor, -15 dBm a target this receiver fails to decode. The modem's own CRC is off, `CRCType = 0x01` (SX126x DS table 13-70), because foreign ADS-L and FLARM bursts carry no such byte. Every `CRC` row is therefore a protocol's own check.

## What the window lets through

The M-band sync window is the 16 chips ADS-L and ALP-TAS share (`core/protocol/air.h`). Noise matches it about once in 65 536 tries at 100 000 tries a second, so a quiet bench frames a few a minute.

The line code tells noise from a burst. §C.2.1 is Manchester, so every chip pair on air is `01` or `10`, and noise decodes half its pairs to a pair no transmitter sends. `protocol::framed_noise` counts those over the first `kNoiseWindowBytes` and cuts at a quarter, five standard deviations below noise and far past any damage the forward correction could repair. The window must end inside the shortest burst on the band, because a dwell reads a fixed 58 chip-bytes and past the last chip the buffer holds chips nobody sent. `air.h` asserts it.

Noise is counted in `air.rx_noise` and never written to the tape. Over an empty sky the count climbs, and where it stops the receiver stopped listening. A `SYNC` row is `Unframed`: a third system heard and not understood.

`Lost` and `Unarmed` both print `LOST`, because to the reader both mean the burst did not go out. The entry and `timing_statistics` keep the distinction.

`Entry::airborne` is per entry because §G.1.16 transmits at 1 Hz in the air and 0.1 Hz on the ground, and the tape outlives a takeoff. `Entry::at_s` is UTC once the receiver has given a second, and time since boot before. `Entry::utc_dated` says which, per entry, because the tape outlives a first fix. The UTC second comes from the latched PPS edge (`timing::ClockState::utc_s`, `core/timing/README.md`).

## The phase, and what two devices read across it

`stamp_of` dates a burst from the executor's own instant (`messages::RfEvent::at_us`) against the latched PPS edge, not from the pass that drained the queue.

The phase is UTC-referenced on every locked device, so one burst sent here and heard there is one instant read twice: `TX .462` and `RX .462` is the link, measured. Both stamps are taken at the end of the burst, where the radio raised it, so they differ by one poll period. A missing row at that phase is a dwell or a channel that did not line up, not a weak signal.

Slot 1 runs from 800 ms to 200 ms into the next second. Dating from the event keeps a burst in that tail in its dwell's second. Dating from the drain would move it a second forward and read as a clock fault.

Without a PPS lock, or past `kStampReachUs`, the row prints the second alone rather than a guessed phase.

`Entry::channel` comes from the executor that armed the dwell (`RfEvent::frequency_hz`), not from the slot map, so it is right exactly when the radio disagrees with the map. §C.2.5 alternates traffic between 868.2 and 868.4 MHz.

A dwell drains its own tail before it closes. Between dwells the receiver is still tuned where it was, so a frame flagged as the window ends belongs to the dwell that closed.

A reception is dated at the instant the executor saw DIO1 go up, or began the status read, never when the read-out ended: reading 58 chip-bytes costs milliseconds. DIO1 stays a level, not an edge, because a deadline drives a dwell (`boards/lilygo/t_echo_plus/t_echo_plus.dts`).

`Entry::tx_keyed_us` runs from the dwell's deadline to the instant the executor issued SetTx (`RfEvent::keyed_at_us`), and `Entry::tx_span_us` to the instant it reported the burst done. The first is this CPU, the rest is the chip and the read-out. The deadline is the drawn instant rounded up to the millisecond (`products/skyblip_go/services/radio.cpp`). §C.2's M-band burst is 4800 us at 100 kchip/s, so a span reads a little over 4800 on silicon, and the log subtracts no nominal length it did not measure. Both saturate at the field's ceiling: a burst 65 ms late is a dwell that already ended, which `Lost` reports. `state.tx_deadline_us` carries the deadline out of `go::RadioService`, which owns it.

Neither figure is on the page. The last burst's pair leaves in the diagnostics dump (`radio tx_keyed_us= tx_span_us=`). On silicon a 6014 us mean span split into 619 us of SPI write and SetTx, 5160 us of ramp and burst, and 234 us for the dwell loop to notice DIO1.

The capacity is one screen: `products/skyblip_go/pages/radio_log.cpp` draws exactly `Log::kMaxEntries` rows.

## What the counters hold

Each verdict that is not a fault has its own counter, so a device with no fix beside a chatty target does not report a climbing bad-frame count.

| | |
|---|---|
| `rx_ok` | `Received` |
| `rx_bad` | `BadCrc` and `Undecoded` |
| `rx_wait` | `Unattempted` |
| `rx_type` | `Unsupported` |
| `rx_unframed` | `Unframed` |
| `rx_miskeyed` | `Miskeyed` |
| `rx_noise` | a sync window the band's own noise walked through |
| `rx_named` | `Named` |
| `rx_status` | `Declared` |
| `tx_ok` | `Transmitted` |
| `tx_lost` | `messages::RfEventType::Missed`: a dwell or transmission of ours that did not complete, never a reception |
| `tx_named` | how many of the bursts in `tx_ok` carried own-ship's registration rather than a position |
| `uplink_stale` | records of decoded uplink packets refused because their G.1.1 timestamp was more than 10 s old or ahead of the second they were heard in (G.3) |
| `tx_given_up` | turns of own-ship's schedule that never went out, neither in their second nor on a retry 2 s on, before their type's next turn (`core/timing/README.md`) |
| `tx_held_for_b5` | seconds own-ship held a position because a close neighbour reads only Issue 1 and the position carried an unknown field Issue 1 has no code for (B.5) |
| `tx_deferred` | `Deferred` bursts (C.2 and D.3, `core/timing/README.md`) |
| `tx_forced` | `Forced` bursts (D.3) |
| `tx_busy_missed` | `Busy` bursts (D.3) |
| `tx_held_for_fix` | positions lost for want of a fix: the solution would have been over 500 ms old on air, less the frame and the key's slack (G.1.16), even on a redrawn instant, or it named no quarter second within 10 ms (G.1.1), or no fix inside its quarter had anchored the clock (`core/timing/README.md`, The age of a position) |
| `tx_redrawn_for_fix` | positions whose fix came after their drawn instant, armed on an instant drawn again over the rest of their window (`core/timing/README.md`, The age of a position) |

`BadCrc` and `Undecoded` share `rx_bad` because a counter answers whether anything is wrong, and both say yes. The tape says which.

All of them reach `core/comms/diagnostics.h`'s radio group, so they leave in the `diagnostics` dump and the `radio` reply under these names. The group is 162 bytes at its widest, one frame on a 182-byte link (`test/core/test_diagnostics_frames.cpp`). A narrower link splits a group over frames that never mix two groups, and `fits()` refuses a link that cannot carry a whole field. The RAW page also prints `rx_status` as `ST`, `uplink_stale` as the fourth figure of `UPLINK` and `tx_held_for_b5` as `B5`. The tape's header shows `RX`, `TX` and `NOISE`.

## Who writes what

`go::TrafficService` writes everything the air reported, because it drains `bus.rf` and alone knows whether a burst also decoded. `go::RadioService` writes `Held` and `Unarmed`, which raise no event: without a row the page would read like a dead transmitter.

`Held` is recorded once per spell, because the allowance holds for minutes and a row a second would push the sky off the tape. `timing_statistics.refused()` counts every refusal, and the next completed burst ends the spell. `Unarmed` is written only for a plan that carried a burst. A refused receive dwell counts in `timing_statistics.missed()`.

## On a bench

Two units on a desk read -6 to -17 dBm, a hundred dB over this modem's floor and into compression, so a bench level says nothing about link margin. Put distance or attenuation between them before believing a level.
