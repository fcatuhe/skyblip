# core/timing

One radio, one second. `slot.h` cuts the second into dwells, `transmit.h` decides whether own-ship speaks in one of them and at which instant, `channel.h` holds what the channel sounds like and what we have already spent on it. Nothing here touches hardware: `ports::Rf` flies the plan against absolute deadlines.

## The burst is placed, and nothing on air moves it

`Transmitter::instant_in()` draws the instant from `mix(address ^ mix(utc))`, uniform over the slot's usable width, re-keyed every second. The executor keys the PA there. It does not listen first, it does not back off, it does not defer to anything it receives: `RfPlan::tx_at_us` is the whole contract, and the only rule that can still refuse a burst is the hour's air time (`AirTime`, below).

This is a deliberate deviation from a plain reading of ADS-L 4 SRD-860 issue 2 §D.3, which describes CSMA with listen-before-talk. Carrier sense cannot do the job the clause implies here, for four reasons.

It cannot see the burst it would collide with. A burst is 5 ms and the direct slot 550 ms wide, so two aircraft collide when their drawn instants land within 5 ms of each other: about 1.8% per neighbour in range. Half of those we drew the earlier instant for, and an assessment only ever detects a burst *already* on air. The theoretical ceiling against our own kind is around 0.9% of bursts saved per neighbour.

It listens in the wrong place. The collision happens at the receiver's antenna and the assessment is made at ours. A third aircraft the other side of us hears both bursts collide while we heard nothing, and we defer to a station the intended receiver cannot hear at all. At these ranges the hidden terminal is the normal case, not the corner one.

It is weighted backwards. A burst has to clear the floor by a good margin to register at all, so the traffic we would successfully defer to is the near, loud traffic whose position we already have, while the distant aircraft near the noise floor, the one whose update matters, is never heard and never deferred to.

And it costs the one thing this device exists for. An aircraft that goes quiet is invisible. What protects the band is the randomised instant, decorrelated by device address and redrawn every second so a collision is not repeated, and the 1% duty cycle this product declares as its channel-access route.

None of the references cancel a burst either. `pjalocha/nrf52-ogn-tracker` (`src/ogn-radio.cpp:840-851`) backs off and escalates, then falls through and transmits when the slot runs out; its loop has no path that drops the packet. `pjalocha/esp32-ogn-tracker` has the code but both `TimeSlot()` call sites pass `MaxWait=0`, which skips it entirely. Neither SoftRF fork has listen-before-talk at all. This device is the same on air as those, with one fewer moving part.

## The schedule is UTC's, not the transmitter's

Airborne, own-ship speaks in every second. On the ground §G.1.16 asks for 0.1 Hz, and that is one second of UTC's own ten: `utc % 10 == mix(address) % 10`. The second is the address's, so the ground population spreads evenly over the ten by construction, and a device that reboots or loses its fix for a minute comes back to the same one rather than to wherever the restart left it.

It used to be a gate: ten seconds since the last completed burst, then wait for the alternating channel's dwell to come round, then draw an instant inside it. That reads 10.2 to 12.5 seconds on a bench, which is under 0.1 Hz whichever way the clause is read, and the interval carried the previous burst's jitter into the next one for ever.

`slot_in()` is the same argument one level down. §C.2.5 alternates the two M-band channels from one transmission to the next, and the clock counts them: `(utc / period) & 1`. Counting our own transmissions instead put the alternation permanently out of step with the grid the first time anything refused a burst, and nothing on the device could see that it had.

What is not the clock's is the instant inside the slot: that stays a fresh draw per second, which is what decorrelates two devices that share a second (above).

## The second a burst is dated by

`ClockState::utc_s` is the UTC second that opened at `utc_edge_us`, and `carry_utc_to_edge()` walks it forward on every latched edge. `OwnshipService` re-anchors the pair whenever a solution's own instant is the edge currently latched.

The receiver names a second in a sentence that lands hundreds of milliseconds inside it, so between the edge and that sentence `own.utc` is a second behind the edge. Everything that dates a burst against the edge - `radio::stamp_of`, the traffic table's now, the ADS-L and ALP-TAS decoders - read `utc_s` through `bus::State::traffic_now()` instead, and a burst drained in that window keeps its own second. Two skyBlips on a bench found this the hard way: one burst read `42:52.954` on the sender and `42:53.956` on the receiver, the same instant, a second apart.

## What the channel measurement is still for

`NoiseFloor` and `ChannelLevel` survive as instruments, not as gates. Each executor reads the tuned channel once per dwell, as a window of `ChannelLevel::kSamples` instantaneous reads averaged in the linear domain, because the SX1262 has no averaging block and GetRssiInst is an instant by definition (DS 13.5.2). One read lands between two neighbours' bursts and calls a loud site quiet; a window does not. `NoiseFloor` walks that figure into a running average, seeded at OGN's -105 dBm so a cold start reads as quiet rather than as broken, and it leaves the device as `noise_dbm` in the status dump.

That number answers a question the counters cannot: a device hearing nothing at a site reading -85 dBm is deaf because the band is full, and one hearing nothing at -110 dBm is deaf for its own reasons. Nothing waits on it.

## The duty cycle is the channel-access route

EN 300 220-2 V3.3.1 Table 4 band M is 1% of any hour, and that limit is what this product declares. `AirTime` is the evidence: sixty one-minute buckets on a ring that turns by elapsed time, so the hour that straddles `millis()`'s 49.7-day wrap is an hour like any other.

At the design rate of one 5 ms burst per second we sit at half the allowance, so an empty budget can only be a fault. It blocks: a faulted transmitter that will not stop is worse for everyone on the band than a quiet one. `Attempt::over_budget` says so, and `SlotTimingStats::refused()` counts it.

## What the dwell map is for

`kSlot1End` is 1200 ms, 200 ms past the second it opened in, because FLARM-generation traffic is still transmitting there. The position burst stops at 1000 where §C.5 ends the direct slot, and `Transmitter::last_instant_in()` bounds the draw so it always completes inside both the slot and the dwell that carries it.

`SlotPlan::own_tx_dwell` is a property of the dwell, not of the phase the service happens to tick on. Slot 0's dwell opens at 400 and its burst is placed from 450, so the plan that opens the dwell has to carry it.

## The tail names us, once every ten seconds

ADS-L carries no callsign. What a pilot is called on the radio reaches the glass and the tablet from OGN's payload type 66, the one §F.2.1 assigns to them and whose definition it leaves to them (`core/protocol/README.md`). Own-ship sends one in slot 1's tail, `kCallsignStart` to `kCallsignEnd`: §C.5 reserves 0..200 and we transmit there anyway, which is a deliberate deviation and the one in this tree that is not forced by physics.

The tail is what it costs least. The dwell is already tuned to §C.2.5's channel 1, so there is no retune; the direct slot keeps every position burst it had; and the 5 ms this spends is 0.05% of the hour, taking the duty cycle from half the allowance to 0.55 of it. What is on air there is FLARM-generation traffic and OGN trackers, whose own slot 2 runs to 1200 (`oss/nrf52-ogn-tracker/src/ogn-radio.cpp:1140-1142`), so a burst in the tail is neither novel nor quiet.

The instant is drawn by the same `mix(address ^ mix(utc))` every other burst is drawn by, over the width the window leaves once the air time and the completion slack are taken off it: 1000 to 1190, which is 191 instants, exactly as many as slot 1's own position window offers between 800 and 990.

The second is the address's own, `Transmitter::callsign_second()`, and never one in which slot 1 carries a position: the executor arms one burst per dwell, so a name that shares slot 1 with a position is dropped. It is even, so airborne the position of that second rides slot 0, and it is never the ground second, so on the ground the two never meet either. A name is only worth sending to a receiver that already has a target for us to hang it on, and OGN's `setTargetCall` drops one for an unknown ID (`oss/nrf52-ogn-tracker/src/lookout.h:527-534`). Airborne, our own position went out earlier in the same second. On the ground it went out at most nine seconds before.

What it costs elsewhere is the flash-write window. `core/timing/durable_write.h` places a settings write inside an armed receive dwell that own-ship cannot transmit in, and until the callsign burst existed slot 1's tail was one of the two such stretches. Now there is one, the uplink dwell, which is also the dwell whose traffic we can most afford to be deaf to.

## The dwell is armed at its edge, the burst when it is due

`ports::Rf::arm()` queues a plan behind the dwell already flying, on silicon because the executor is a thread that reads its plan once, and on the host because it models the same rule. A plan queued that way is read when the flying dwell ends, by which time its own window has closed, so it is dropped.

A burst cannot wait for that. Whether one may go out is decided on the fix, the rate and the slot, and those clear when they clear: a solution that lands after 400 ms would cost the whole second if the dwell were armed at its edge and never looked at again. So a second plan for the channel the dwell is already flying, carrying a burst that fits inside the window it is already in, is not the next dwell: it is this dwell's burst, and `arm()` hands it to the dwell in flight instead of queueing it. On silicon that crosses a thread boundary, published under the scheduler lock and read by the dwell loop; on the host it is the same rule in one thread, which is why the suite can hold it.

A window already behind the phase is not armed at all. The guard phases between dwells (`SlotState::Hop`, `SwitchOtoM`) report the dwell that has just closed, and arming that plan produced a stub of a millisecond or two that the executor could only drop. A dropped plan carrying no burst is silent now: it is a receive dwell that did not happen, not a transmission that failed, and the station log said `LOST` for it.
