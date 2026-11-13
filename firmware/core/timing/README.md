# core/timing

One radio, one second. `slot.h` cuts the second into dwells, `transmit.h` decides whether own-ship speaks in one of them and at which instant, `channel.h` holds what the channel sounds like. Nothing here touches hardware: `ports::Rf` flies the plan against absolute deadlines.

The rules ADS-L sets for a transmitter live in its stack (`protocols/adsl/README.md`): the slots, the rates, the two channels and the Status period. `Transmitter` composes them with what skyBlip decided: the channel each second carries, the 20 s cycle a unit owns and where in it each burst goes, the instant drawn inside each window, the window each dwell leaves a burst, the radio's completion guard, and the hour's air time it declares as its channel-access route.

## The burst is placed, and nothing on air moves it

`Transmitter::instant_in()` draws the instant from `hash(address ^ hash(utc_s))`, uniform over the direct slot clipped to the dwell that carries the burst, re-keyed every second. The executor keys the PA there. It does not listen first, it does not back off, it does not defer to anything it receives: `RfPlan::tx_at_us` is the whole contract, and the only rule that can still refuse a burst is the hour's air time (`AirTime`, below).

This is a deliberate deviation from a plain reading of ADS-L 4 SRD-860 issue 2 §D.3, which describes CSMA with listen-before-talk. Carrier sense cannot do the job the clause implies here, for four reasons.

It cannot see the burst it would collide with. A burst is 5 ms and the direct slot 550 ms wide, so two aircraft collide when their drawn instants land within 5 ms of each other: about 1.8% per target in range. Half of those we drew the earlier instant for, and an assessment only ever detects a burst *already* on air. The theoretical ceiling against our own kind is around 0.9% of bursts saved per neighbour.

It listens in the wrong place. The collision happens at the receiver's antenna and the assessment is made at ours. A third aircraft the other side of us hears both bursts collide while we heard nothing, and we defer to a station the intended receiver cannot hear at all. At these ranges the hidden terminal is the normal case, not the corner one.

It is weighted backwards. A burst has to clear the floor by a good margin to register at all, so the traffic we would successfully defer to is the near, loud traffic whose position we already have, while the distant aircraft near the noise floor, the one whose update matters, is never heard and never deferred to.

And it costs the one thing this device exists for. An aircraft that goes quiet is invisible. What protects the band is the randomised instant, decorrelated by device address and redrawn every second so a collision is not repeated, and the 1% duty cycle this product declares as its channel-access route (below).

None of the references cancel a burst either. `pjalocha/nrf52-ogn-tracker` (`src/ogn-radio.cpp:840-851`) backs off and escalates, then falls through and transmits when the slot runs out; its loop has no path that drops the packet. `pjalocha/esp32-ogn-tracker` has the code but both `TimeSlot()` call sites pass `MaxWait=0`, which skips it entirely. Neither SoftRF fork has listen-before-talk at all. This device is the same on air as those, with one fewer moving part.

## The second a burst is dated by

`ClockState::utc_s` is the UTC second that opened at `utc_edge_us`, and `carry_utc_to_edge()` walks it forward on every latched edge. `OwnshipService` re-anchors the pair whenever a solution's own instant is the edge currently latched.

The receiver names a second in a sentence that lands hundreds of milliseconds inside it, so between the edge and that sentence `own.utc_s` is a second behind the edge. Everything that dates a burst against the edge - `radio::stamp_of`, the traffic table's now, the ADS-L and ALP-TAS decoders - read `utc_s` through `bus::State::traffic_now_s()` instead, and a burst drained in that window keeps its own second. Two skyBlips on a bench found this the hard way: one burst read `42:52.954` on the sender and `42:53.956` on the receiver, the same instant, a second apart.

## What the channel measurement is still for

`NoiseFloor` and `ChannelLevel` survive as instruments, not as gates. Each executor reads the tuned channel once per dwell, as a window of `ChannelLevel::kSamples` instantaneous reads averaged in the linear domain, because the SX1262 has no averaging block and GetRssiInst is an instant by definition (DS 13.5.2). One read lands between two targets' bursts and calls a loud site quiet; a window does not. `NoiseFloor` walks that figure into a running average, seeded at OGN's -105 dBm so a cold start reads as quiet rather than as broken, and it leaves the device as `noise_dbm` in the status dump.

That number answers a question the counters cannot: a device hearing nothing at a site reading -85 dBm is deaf because the band is full, and one hearing nothing at -110 dBm is deaf for its own reasons. Nothing waits on it.

## The duty cycle is the channel-access route

EN 300 220-2 V3.3.1 Table 4 band M is 1% of any hour, and that limit is what this product declares in place of §D.3's listen-before-talk. `AirTime` is the evidence: sixty one-minute buckets on a ring that turns by elapsed time, so the hour that straddles `milliseconds()`'s 49.7-day wrap is an hour like any other.

`Transmitter` asks `AirTime` before every burst and spends it after. At the design rate of one 5 ms position burst per second, a Status and a name every ten seconds (below), we spend 6 ms a second and sit at 0.6 of the allowance, so an empty budget can only be a fault. It blocks: a faulted transmitter that will not stop is worse for everyone on the band than a quiet one. `Attempt::over_budget` says so, and `SlotTimingStatistics::refused()` counts it.

## What the dwell map is for

`kSlot1EndMs` is 1200 ms, 200 ms past the second it opened in, because FLARM-generation traffic is still transmitting there. The position burst stops at 1000 where §C.5 ends the direct slot, and `Transmitter::last_instant_in()` bounds the window each instant is drawn over, so the burst always completes inside both the slot and the dwell that carries it.

`SlotPlan::own_tx_dwell` is a property of the dwell, not of the phase the service happens to tick on. Slot 0's dwell opens at 400 and its burst is placed from 450, so the plan that opens the dwell has to carry it.

## The seconds each burst owns

The second alone picks the channel. A position goes on channel A, 868.2 MHz in slot 0, in an even UTC second, and on B, 868.4 MHz in slot 1's direct part, in an odd one, on the ground as well as airborne (`Transmitter::slot_in()`). The Status (§G.2) and the name go on the other channel from the position of their second, B in an even second and A in an odd one (`extra_slot_in()`). So no extra ever shares a dwell with a position, and no dwell carries more than one burst of ours.

Each unit owns a 20 s cycle that starts at its ground second, `g = serial % 10`. The serial is the low 16 bits of the address, and skyblip.eu hands serials out densely from 1, so a fleet spreads exactly evenly over the ten starting seconds and ten consecutive serials never share one. On the ground the cycle holds six bursts, at the same offsets from g for every unit (`kPositionOffsetsS`, `kStatusOffsetsS`, `kCallsignOffsetsS`):

| Offset | Burst | Channel for an even g |
|---|---|---|
| +0 | position | A |
| +2 | Status | B |
| +7 | name | A |
| +9 | position | B |
| +13 | Status | A |
| +16 | name | B |

An odd g mirrors the channels. Airborne the position goes every second, and the Status and the name go in the same seconds and on the same channels as on the ground, so a takeoff or a landing changes nothing about them.

A minute for g = 0, A being 868.2 MHz at 450 to 793 ms and B 868.4 MHz at 800 to 993 ms. The 20 s cycle repeats at 20 and 40:

| Second | Airborne | On the ground |
|---|---|---|
| 0 | position A | position A |
| 2 | position A, Status B | Status B |
| 7 | position B, name A | name A |
| 9 | position B | position B |
| 13 | position B, Status A | Status A |
| 16 | position A, name B | name B |
| any other | position, A in an even second, B in an odd one | |

On the ground channel A carries 0, 7, 13, 20, 27, 33, 40, 47 and 53, and channel B 2, 9, 16, 22, 29, 36, 42, 49 and 56.

Why these offsets:

- The parity rule would put two positions 10 s apart on the same channel, which §C.2.5's alternation forbids, so the ground positions are 9 then 11 s apart. That is skyBlip's reading of §G.1.16's "0.1 Hz (once every 10 seconds)": exactly 0.1 Hz on average, the 11 s gap coming from §C.2.5 with the parity rule.
- Every placement of the six bursts was checked, and this one gives the most even spacing on each channel, which is what a receiver on one channel sees: 7, 6, 7 s on one and 7, 7, 6 s on the other, never more than 7. Across both channels the bursts are 2 to 5 s apart.
- Every type alternates channel from one transmission to the next: positions, Status (§C.2.5) and names. Each channel carries a Status every 20 s, so a receiver that listens on one channel only still gets §G.2.11's rate, and the Status gaps alternate 11 and 9 s.
- Two bursts of ours on one frequency are always at least 100 ms apart, which is EN 300 220-1's off-time.

The instant inside a window is drawn from the address and the UTC second, so two units that do share a second still do not share an instant: the position from `hash(address ^ hash(utc_s))`, the Status from one more round of the hash and the name from two more, so no two bursts share a draw (`Transmitter::extra_instant_in()`). Each is drawn over its whole window, since nothing else of ours is in that dwell. The air time is one position a second airborne plus a Status and a name every ten seconds, 6 ms a second, 0.6 of the allowance, and 0.15% of the hour on the ground. `test/core/test_transmit.cpp` checks the offsets, the per-channel gaps, the alternation of each type, the one burst a dwell and the 100 ms off-time over 1024 serials and a whole minute, airborne and on the ground.

The alternation is the clock's, so a burst refused for an hour's air time spent or a dwell not armed leaves a gap and puts nothing out of step.

## The name rides the direct slot

ADS-L carries no callsign. What a pilot is called on the radio reaches the glass and the tablet from OGN's payload type 66, a broadcast type §F.2.1 lists and leaves to OGN (`core/protocol/README.md`). It goes out like any other ADS-L payload, in the direct slot of the channel its second's position leaves free. It used to go in slot 1's tail, 1000 to 1193 ms, which §C.5 reserves, and that was the one deliberate deviation from the clause this tree made. With it gone, the tail is receive only again.

A name is only worth sending to a receiver that already has a target for us to hang it on, and OGN's `setTargetCall` drops one for an unknown ID (`oss/nrf52-ogn-tracker/src/lookout.h:527-534`). Airborne the target is a second old at most. On the ground the name follows a position by 7 s, inside the ten seconds any receiver has to keep a ground target to show it at all.

The tail pays it back to the flash-write window. `core/timing/durable_write.h` places a settings write inside an armed receive dwell that own-ship cannot transmit in, and slot 1's tail is one of the two such stretches again, beside the uplink dwell.

## The next dwell is armed ahead, the burst when it is due

The service loop sleeps 10 ms between passes, so a dwell armed by the pass that noticed its edge opened up to a pass late, and further behind on a pass that ran long. The two captures of 2026-09-27 had slot 1 opening 3 to 6 ms late at the median and up to 120 ms late at worst, and a slot 1 burst drawn for the milliseconds before the arm was never sent. So `RadioService` hands the executor the next dwell as soon as the current one flies, at the absolute instant the latched PPS edge names for it, and the edge between the two is the executor's to keep on its own thread. `ports::Rf::arm()` queues that plan behind the dwell in flight, and the executor switches into it as soon as the flying one ends, never more than `kSwitchLeadMs` ahead of its start, so each guard is spent retuning and the dwell is listening from its first microsecond. The one guard the map names after the dwell it leads to, 200 to 205, is the executor already retuning into the uplink dwell, and the service arms nothing in it.

A burst cannot always be decided that early. Whether one may go out is decided on the fix, the rate and the slot, and those clear when they clear: a solution that lands after 400 ms would cost the whole second if the dwell were armed ahead and never looked at again. So the next dwell is queued bare when its burst is not decided yet, and queued again carrying it the pass its gates clear. And a second plan for the channel the dwell is already flying, carrying a burst that fits inside the window it is already in, is not the next dwell: it is this dwell's burst, and `arm()` hands it to the dwell in flight instead of queueing it. A dwell carries up to two bursts, `ports::RfBursts`, keyed in the order of their instants whichever was armed first, and a burst armed again at an instant the dwell already carries is that burst, not a second one. A burst armed for the queued dwell joins it rather than replacing it, so a burst decided early is not dropped by a plan that arrives later. Every TxDone and every Missed carries the instant its burst was armed for, which is how the tape tells a dwell's two bursts apart. `abort()` cuts the flying dwell short and drops the queued one. A plan that ends inside the flying dwell and cannot join it is refused, never queued: queued, it would have taken the place of the next dwell and then expired. On silicon all of that crosses a thread boundary, published under the scheduler lock; on the host it is the same rule in one thread, which is why the suite can hold it.

The two dwells the executor holds each carry their own burst buffer, because the burst queued on the next dwell is built while the flying one may still be waiting to key. The durable-write policy reads only the flying dwell's burst: a burst queued behind it is not in the air yet.

A window already behind the phase is not armed at all. The guard phases between dwells (`SlotState::Hop`, `SwitchOBandToMBand`) report the dwell that has just closed, and arming that plan produced a stub of a millisecond or two that the executor could only drop. A dropped plan carrying no burst is silent: it is a receive dwell that did not happen, not a transmission that failed.

What each switch costs is the executor's own measurement, from the retune's first command to the SX1262 reporting the new mode, reported by the `timing` command as the worst hop, the worst change to each band, and the dwells armed ahead that were still not listening at their start. Those figures are what the guards should be sized from. The retune itself goes through STDBY_XOSC and falls back there after a burst, because STDBY_RC unpowers the TCXO and the receiver then waits its whole 5 ms start (SX1261/2 datasheet §13.3.6); the hop between the two M-band channels writes the frequency and nothing else.
