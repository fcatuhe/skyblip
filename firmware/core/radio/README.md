# core/radio

The station log: every burst this radio sent or heard, in the order it happened, newest first.

`rx_ok` and `tx_ok` are totals, and a total cannot tell an empty sky from a receiver that frames nothing. The traffic table only ever holds what already decoded, so a burst that arrived and did not become a frame leaves no trace in it. That burst is the one worth seeing: it is the difference between "nobody is transmitting" and "everybody is transmitting and I am deaf to them", and the two have the same reading on every other page. Two skyBlips that both transmit and neither hears is the fault this exists for, and it happened: `git log core/protocol/air.cpp`.

`Event` names the twelve things that can happen to a burst.

| | |
|---|---|
| `Transmitted` | own-ship's burst left the antenna |
| `Lost` | own-ship's burst was armed and never completed: the dwell ran out, or the radio's transmit timeout did |
| `Held` | the hour's air-time budget refused it (`timing::Transmitter::Attempt::over_budget`) |
| `Unarmed` | `ports::Rf` refused the plan that carried it, so nothing was ever armed |
| `Received` | a burst arrived, framed, and named an aircraft |
| `Named` | a burst arrived, framed, and carried an aircraft's registration instead of its position: §F.2.1's payload 66 with OGN's info header, which `TrafficService::decode_adsl` reads for the callsign and files under the sender's address (`core/traffic/callsigns.h`) |
| `BadCrc` | an integrity check refused it: the chip's own CRC, an ADS-L or ALP-TAS CRC no forward correction could rescue, or a Reed-Solomon codeword the uplink could not correct |
| `Unframed` | clean chips behind the sync window and a sync tail naming neither system, so nothing was ever attempted |
| `Miskeyed` | an ALP-TAS frame whose CRC held and whose sender keyed it on a second inside `kAlptasKeyWindowS` of ours, but not ours |
| `Undecoded` | the frame was attempted and refused: the ALP-TAS plausibility gate with no second that would have read it, or an ADS-L frame carrying no position |
| `Unsupported` | the frame framed and carried a message type this firmware does not implement: ALP-TAS reads its 2024 position frame and nothing else, so Air V6 (type 0) and text messages (type 3) land here, and ADS-L reads §F.2.1's Traffic and the registration inside payload 66, so Status (3), Remote Identification (6) and any other telemetry carried in 66 land here too |
| `Unattempted` | nothing was tried, because own-ship had no fix or no UTC to decode an ALP-TAS frame against |

`BadCrc` and `Undecoded` were one verdict until the bench had two devices on it, and the pair of them is the reading that separates a marginal link from a protocol disagreement. Bits the air corrupted are a radio problem, and bits nothing here knew what to do with are ours: one is answered by moving the antenna and the other by reading `core/protocol/`.

The last three were one verdict until 2026-09-19, and two of them were never a fault. A skyBlip still acquiring printed `DEC` for every ALP-TAS burst a SoftRF two metres away put on the air, for the whole minute it took to find the sky: an ALP-TAS position is coded relative to the receiver and its key stage is derived from the UTC second, so `TrafficService::decode_alptas` cannot start without a fix and a date, and a burst it never attempted is not a burst that failed. The ADS-L path has no such gate and needs none, because §G.1 carries an absolute position: an ADS-L frame decodes on a device that has never seen a satellite.

`Unframed` and `Miskeyed` came off `Undecoded` on 2026-09-19, for the same reason `WAIT` and `TYPE` did before them: neither is this firmware getting a frame wrong. A burst whose sync tail names no system was never attempted, and the tape used to spell that `DEC` beside a frame we read and refused. `Miskeyed` is the other half, and it is the verdict this file was rewritten for. The ALP-TAS key stage moves once every 16 seconds (`utc >> 4`) and the frame carries the low 4 bits of the second its sender keyed in, so the decode tolerates a one-second disagreement and nothing more. Two seconds out, or across the block boundary, and every frame from that emitter fails for as long as the two clocks disagree: total, then gone, with no other symptom on the glass. `protocol::alptas_keyed_second` walks the blocks the window reaches, reads the time bits out of whichever one decrypts into a structured frame, and the row prints the offset as `KEY+18` or `KEY-2`. That number is the whole diagnosis, and the frame is still refused: traffic keyed on a second we disagree with is not traffic this device will draw.

A refused frame past the CRC names its sender. The ALP-TAS address word is in the plaintext first two words and the frame CRC covers it, so `TYPE`, `KEY` and `DEC` rows carry the address beside the verdict (`Entry::addr_valid`). Before that, a bench had a tape of refusals and no way to tell whose frames they were: one emitter two metres away and a sky full of others read identically.

`Unsupported` is the neighbour's dialect rather than our failure, on both systems. `protocol::alptas_decode` refuses anything that is not message type 2 before it decrypts, and the type sits in the plaintext first word, so a SoftRF built with `USE_INTERLEAVING` - which interleaves Air V6 with Air V7 - reads as a stream of frames this firmware deliberately does not read. ADS-L asks the same question one field later: the payload type is the first byte past the descramble, and §F.2.1 puts four other broadcast payloads on this band beside Traffic. Since 2026-09-20 one of those four is read: payload 66 is OGN's, and a registration inside it is `Named` rather than a dialect we skip. The others - Status, Remote Identification, and payload 66 carrying anything but a registration - are valid frames from conforming senders and none of them an aircraft. What is left on `Undecoded` is a frame of a type we do read that framed, passed its own check and was still refused - an ADS-L Traffic payload carrying no position, a registration whose text is not a callsign (`protocol::is_callsign_char`), or an ALP-TAS frame the plausibility gate threw out - which is the only one of the three that means something may be wrong.

A `CRC` row is a frame that framed and failed its own protocol's check, which is a real emitter the air damaged. It carries the byte count and the level it arrived at: `GetPacketStatus` survives the failure, and -110 dBm against -15 dBm is the difference between a burst at the floor and a neighbour this receiver is failing to decode.

Until 2026-09-16 the modem was programmed with `CRCType = 0x00`, which the SX126x spells one-byte CRC: off is `0x01` (DS table 13-70). The two units on the bench interoperated because they made the same mistake symmetrically, and every foreign ADS-L or FLARM burst was thrown away by a CRC byte its sender never sent. With the chip's check off it can no longer refuse anything, so the modem's own `CrcError` is a branch nothing on this hardware reaches and every `CRC` row is now a protocol's own verdict.

## What the window lets through

The M-band sync window is 16 chips, because that is all ADS-L and ALP-TAS share (`core/protocol/air.h`), and 16 chips is thin: noise matches it about once in 65 536 tries and the correlator takes 100 000 tries a second. A quiet bench frames a few of those a minute. They are not receptions. Nothing arrived.

What tells them from a burst is the line code, not the level. §C.2.1 is Manchester, so every chip pair on air is `01` or `10`: noise decodes half of its pairs to a pair no transmitter can send, and a burst decodes none of them that way. `protocol::framed_noise` counts those over the first `kNoiseWindowBytes` and cuts at a quarter of them, five standard deviations below what noise produces and far past any damage the forward correction could repair.

The count stops there and not at the end of the report because a dwell reads a fixed 58 chip-bytes and the shortest system behind this window fills fewer: past its last chip the buffer holds chips nobody sent, which decode as damage and are evidence of nothing. The window has to end inside the shortest burst the band carries, and `air.h` asserts that it does.

A window noise walked through is counted in `air.rx_noise` and written nowhere else. Sixteen rows is the whole tape, and a row describing nothing that arrived is a row the sky could have used. The count is on the page because it is the receiver's heartbeat: over an empty sky it climbs, and a site where it stops has a receiver that stopped listening rather than a band that went quiet. What earns a `SYNC` row is the other kind - clean chips behind the window and a sync tail naming no system - which is a third system on these frequencies, heard and not understood.

There is no verdict for a burst the radio declined to send, because nothing declines: the instant is drawn inside the slot and the PA keys there whatever the receiver is hearing (`core/timing/README.md`). A dwell that carried a burst and reported nothing is `Lost`.

The page spends no row on the difference between `Lost` and `Unarmed`: both read `LOST`, because both mean the burst did not go out and what separates them is ours, not the reader's. The distinction survives where it is acted on, in the entry and in `timing_stats`.

`Entry::airborne` is own-ship's flight state when the row was written, and the page prints it as `AIR` or `GND` on every burst of ours. It is per entry for the same reason the date is: §G.1.16 transmits at 1 Hz in the air and 0.1 Hz on the ground, and the tape outlives a takeoff. The page used to draw the live flag on all sixteen rows, so one takeoff rewrote the schedule the whole tape claimed to have gone out on.

`Entry::at_s` is UTC once the receiver has given us a second, and time since boot before that. Which of the two is a flag per entry rather than a flag on the log, because the log outlives a first fix and the entries either side of one are dated differently. The second itself comes from the PPS edge the clock last latched (`timing::ClockState::utc_s`), not from the sentence that names it: see `core/timing/README.md`.

## The phase, and what two devices read across it

`stamp_of` dates a burst from the instant the executor stamped it with (`messages::RfEvent::at_us`), against the PPS edge `timing::ClockState` latched, rather than from the pass that drained the queue. Three things follow, and the first is the only one the page exists for.

The phase is UTC-referenced on every device that holds a lock, so a burst sent on one skyBlip and heard on another is one instant read twice: `TX .462` here and `RX .462` there is the link, measured, with nothing between the two units. Both stamps are taken where the radio raised the burst - the end of it, either way - so what is left between them is one poll period, and a missing row at that phase is a dwell or a channel that did not line up rather than a weak signal.

Slot 1 opens at 800 ms and closes 200 ms into the next second, so a burst in that tail is drained under a second the air had not reached when it was sent. Dating from the event's own instant puts it back in the second its dwell opened in; dating from the drain would have moved a burst a second forward and read as a clock fault on the device that heard it.

A phase is refused rather than guessed. Without a PPS lock there is no edge to measure from, and past `kStampReachUs` the event and the edge are not two views of one second. The row then prints the second alone, which is what the receiver actually knows.

`Entry::channel` is the dwell's own, carried on the event from the executor that armed it (`RfEvent::freq_hz`): §C.2.5 makes traffic alternate between 868.2 and 868.4 MHz, and a receiver that never visits the second channel hears half its neighbours. Deriving it from the phase instead would have agreed with the slot map exactly when the radio did not, which is the one case worth printing.

For that label to be true, a dwell drains its own tail before it closes. A burst framed as the window ends leaves its flag in the chip, and the next dwell's first poll used to read it and stamp it with the new band, the new channel and the new phase. Clearing the register on the way in would have been worse: between two dwells the receiver is still tuned where it was, so a frame arriving there is a real reception on the dwell that just closed, and that is the one it is reported as.

`Entry::tx_span_us` is the burst's own completion: from the deadline the dwell was armed for to the instant the executor reported it done. That deadline is the drawn instant rounded up to the millisecond `phase_ms()` was read at (`services/radio.cpp`), so the burst reaches the air up to a millisecond later than the draw and this figure cannot see it. §C.2's M-band burst is 4800 us of that at 100 kchip/s, and the rest is the pass that collected the report, so the figure reads a little over 4800 on silicon and a good deal more wherever the radio thread is being starved. It is not a jitter against the deadline: the report lands at the end of a burst that the deadline keys the start of, and subtracting a nominal length to make it read zero would be this file forming an opinion about air time it did not measure. `state.tx_deadline_us` carries the deadline out of `go::RadioService`, which owns it, for the same reason `last_tx_done_at_us` travels the other way: neither service forms a second opinion about the other's half.

`Entry::tx_keyed_us` is the first half of that, from the same deadline to the instant the executor issued SetTx (`RfEvent::keyed_at_us`). One number could not say whether a slow burst was slow before the air or after it, and both halves have different causes: what happens before is this CPU, what happens after is the chip and the read-out. The pair saturates at the field's own ceiling rather than at a column width - a burst 65 ms past its deadline is a dwell that has already ended, and the `Lost` verdict is what reports that.

Neither figure is on the page. Both are microseconds of this CPU and of the chip, and a reader who wants them is at a bench with the link open, so the last burst's pair leaves through the diagnostics dump (`radio tx_keyed_us= tx_span_us=`) and the page keeps the row for the sky. What the pair measured once is worth keeping: of a 6014 us mean span on silicon, 619 us is the SPI write and SetTx before the air, 5160 us is the ramp and the burst itself, and 234 us is the dwell loop noticing DIO1.

A reception is dated where the radio raised it: the executor stamps the instant it saw DIO1 go up, or the instant it began the status read, never the instant the read-out ended. Reading 58 chip-bytes out of the buffer costs milliseconds on a bench, and it used to sit between the burst and its own timestamp, which is what made one link's two stamps read 2 to 3 ms apart. DIO1 stays a level, not an edge: a dwell is driven by a deadline (`boards/lilygo/t_echo_plus/t_echo_plus.dts`).

The capacity is what one screen holds. Nothing is kept that could not be shown: this is a tape of what is happening now, not a history to scroll back through. `products/skyblip_go/pages/radio_log.cpp` draws exactly `Log::kCapacity` rows for that reason.

## What the counters hold

The verdicts split on 2026-09-19 and the counters followed on the same day, because until then every outcome that was not `Received` was `air.rx_bad`. A device on the apron with no fix beside a chatty neighbour reported a climbing bad-frame count while behaving perfectly, on the panel and in every reply a companion app reads, which is the mislabelling `WAIT` and `TYPE` had just been taken out of the tape for.

| | |
|---|---|
| `rx_ok` | a burst that named an aircraft |
| `rx_bad` | a reception that is a fault: framed, attempted, refused, plus the integrity failures |
| `rx_wait` | `Unattempted`, the bursts no fix or no UTC let us try |
| `rx_type` | `Unsupported`, the dialect we do not read |
| `rx_unframed` | `Unframed`, a burst that named neither system |
| `rx_miskeyed` | `Miskeyed`, a frame whose sender keyed another second |
| `rx_noise` | a sync window the band's own noise walked through |
| `rx_named` | `Named`, a registration heard from somebody else |
| `tx_ok` | own-ship's burst left the antenna |
| `tx_lost` | own-ship's burst was armed and never completed |
| `tx_named` | how many of the bursts in `tx_ok` carried own-ship's registration rather than a position |

`BadCrc` is counted in `rx_bad` beside `Undecoded` rather than apart from it, even though the two are answered differently. What a counter is read for is whether anything is wrong, and both say yes: a burst the air damaged and a burst nothing here could read are each a reason to go and look. Which of the two it was is the tape's column, one row per burst, and a number that only ever climbs cannot carry that distinction anyway.

`tx_lost` is `messages::RfEventType::Missed`, which is only ever emitted for a dwell or a transmission of ours that did not complete and was never a reception at all. It was counted as a bad reception until the same day, which is [#61](https://github.com/fcatuhe/skyblip/issues/61), now closed.

All eleven reach `core/comms/diagnostics.h`'s radio group, so they leave on the USB console line, in the `diag` dump and in the `radio` reply, under the names above. The radio group no longer fits one 182-byte notification even at rest, and answers in two: the dump has been framed per group since it existed, and `fits()` still refuses a link that cannot carry a whole field. The tape's own header keeps `RX`, `TX` and `NOISE`, because the panel's reader of `WAIT` and `TYPE` is the sixteen rows underneath it.

## Who writes what

`go::TrafficService` writes everything the air reported. It already drains `bus.rf`, and it is the one place that knows whether a burst that arrived also decoded, which is the distinction the whole page turns on.

`go::RadioService` writes the two the air never sees. A burst its own policy refused reaches no executor and raises no event, so before `Held` and `Unarmed` existed the counters moved and the page stayed silent - which reads exactly like a dead transmitter to the one person looking at it. `Held` is the hour's allowance holding every burst until it frees up, and `Unarmed` is the executor refusing a plan that cannot complete inside its own dwell.

`Held` is recorded once per spell, not once per refusal. The allowance holds for minutes at a time and a row a second would push the sky itself off a sixteen-row tape, so the row says when the spell began and `timing_stats.refused()` counts every one. The next completed burst ends the spell, and a refusal after that is a new one. `Unarmed` is written only for a plan that carried a burst: a receive dwell the executor refused is a deafness, not a transmission, and it keeps its counter in `timing_stats.missed()` until the page has somewhere honest to put it.

## On a bench

Two units on a desk read -6 to -17 dBm, which is a hundred dB over this modem's floor and into compression. Nothing measured there says anything about link margin: put distance or attenuation between them before believing a level. What a bench does answer is the column of verdicts, and the honest reading of a quiet band is an empty tape under a noise count climbing a few a minute.
