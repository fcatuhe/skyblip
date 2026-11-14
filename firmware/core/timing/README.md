# core/timing

One radio, one second. `slot.h` cuts the second into dwells, `transmit.h` decides whether own-ship speaks in one of them and at which instant, `channel.h` holds what the channel sounds like and how own-ship shares it. Nothing here touches hardware: `ports::Rf` flies the plan against absolute deadlines.

The rules ADS-L sets for a transmitter live in its stack (`protocols/adsl/README.md`): the slots, the rates, the two channels and the Status period. `Transmitter` composes them with what skyBlip decided: the channel each second carries, the 20 s cycle a unit owns and where in it each burst goes, the retry 2 s later, the instant drawn inside each window, the window each dwell leaves a burst, the radio's completion guard, the name in slot 1's tail, the hour's air time it declares as its channel-access route, and the draw that randomises a deferral. `RadioService` applies §D.3's listen-before-talk on top of them.

## The burst is drawn, then listened for

`Transmitter::instant_in()` draws the instant from `hash(address ^ hash(utc_s))`, uniform over the direct slot clipped to the dwell that carries the burst, re-keyed every second. The executor listens before it keys there. ADS-L 4 SRD-860 issue 2 §D.3 asks for listen-before-talk "according to the Polite Spectrum Access rules laid out in ETSI EN 300 220-1", and that standard's clause 5.21 is what follows. The figures the standard prints are in `protocols/common/en300220.h`, ADS-L's own rule is `adsl::media_access()` (`protocols/adsl/media_access.h`), and what skyBlip declares under both is `PoliteAccess` in `channel.h`.

The executor reads the channel over the CCA interval, a window of nine instant reads 200 µs wide averaged as power (`ChannelLevel`), and the window ends at most the declared dead time, 2 ms, before the burst starts. It reads the channel and band of the burst, which the radio is already receiving on in the dwell, so nothing retunes. Slot 1's window opens 3 ms after its dwell (`PoliteAccess::kCcaLeadUs`), so the reading never falls in the hop before it. Below the threshold the burst keys at its instant. At or above it nothing keys, and the executor reports `RfEventType::Busy` with the level it read. A radio found out of receive at that moment cannot say the channel is clear, so it reports busy with no level. Both executors start reading 1.2 ms before the instant (`platform::kCcaLeadUs`, `hardware/platform/clear_channel.h`): 0.7 ms of staging on the nRF52, the 200 µs window and 0.3 ms for reads that overrun it, an allowance no bench capture has measured yet. The host executor models the same lead, so the simulator listens when the device does. The dead time is enforced, not assumed: a burst that would key more than 2 ms after its reading ended does not key. The nRF52 executor, whose radio is in standby with the burst staged by then, reports it busy with no level, and the host executor listens again. Once the reading starts, the burst is the executor's (`RfMailbox::claim()`): a `withdraw()` that arrives during it is refused, and the burst keys or is reported busy.

The threshold is EN 300 220-1 Table 45's for a device under 100 mW e.r.p.: 15 dB above Table 32's sensitivity limit S = 10 log RB - 117 dBm. RB is ADS-L's 200 kHz channel, so S is -93.99 dBm and the threshold -78.99 dBm at a 0 dBd antenna. Table 45's note asks for the limit to be "adjusted accordingly" for an antenna gain other than 0 dBd, and its example says how: "With antenna gain of -5 dB, the CCA threshold shall be corrected and increased by 5 dB at the RF front end." The threshold is the 0 dBd limit less the antenna's gain in dBd, and the antenna's gain only. The T-Echo's antenna peaks at 1.6 dBi, which is -0.55 dBd (`parts::sx1262::kAntennaGainCentiDbd`), so the limit at the antenna connector, where a conducted CCA test measures, is -78.99 + 0.55 = -78.44 dBm. The feed costs 0.5 dB between the connector and the chip that reads the level (`kFeedLossCentiDb`), so the chip calls the channel busy from -78.94 dBm, from -78 dBm in the whole dBm it reads (`timing::cca_threshold_centi_dbm()`, `platform::kCcaThresholdCentiDbm`). 200 kHz is the conservative RB. The SX1262 filter this dwell programs is 234.3 kHz wide, which would put the limit 0.7 dB higher. The antenna gain and the feed loss are the datasheet's and an allowance, not measured on a unit. The SX1262's RSSI is not calibrated either, and the threshold carries no margin for its error: until a bench measurement sets one, that is a known limit of the declaration.

`RadioService` applies §D.3 to every busy report (`radio_access.cpp`). A busy burst is deferred by a random whole number of 5 ms units, one ADS-L burst, as 5.21.1's note asks the interval to match the device's own transmissions. The draw picks 1 to 8 units from the address and the busy instant (`Transmitter::deferral_draw()`), counted from the instant the channel was busy, so a deferral is never longer than 40 ms. It is never earlier than the executor can listen again: a service that hears of the busy channel late draws from the units left, and one that hears of it more than 40 ms late misses the burst (`adsl::media_access()`). The burst is then assessed again, inside its own window: the direct-slot window of its dwell, or slot 1's tail for the name. A burst never moves into another dwell or onto the other channel: §C.2.5's alternation gives the second its channel, so the device takes none of EN 300 220-1 5.21.4's frequency agility. A position is encoded again for its new instant, from the newest fix: its TimeStamp names the instant it goes out at and its position is carried forward to it. G.1.16's 500 ms nav age was checked when the burst was first armed, measured to the top of its second as this device always measures it, and a deferral never leaves that second.

When the window runs out with the channel still busy, the burst counts as missed and the retry rule takes it, 2 s later on the same channel (below). An airborne position is never retried: the next second's covers it.

§D.3 lets a device force a packet that could not go out 3000 ms after its initial attempt. skyBlip forces only on the ground, a Status, a name or a position, whose next turn is 9 to 11 s away. It never forces airborne, where the 2000 ms of silence after a forced Status or name would cost two positions. The initial attempt is the first busy assessment of the turn's burst, kept across its retries (`RadioService::Contention`). The forced burst goes back to the executor with `RfPlan::tx_forced`, which keys it without listening. For the 2000 ms after it ends, its instant plus its air time, nothing of ours goes on air: a burst already handed to the executor inside that span is withdrawn (`ports::Rf::withdraw()`), and none is armed there, so the schedule sees missed bursts and the retry rule applies.

Deferrals, forced bursts and bursts missed with the channel busy are counted in `air.tx_deferred`, `air.tx_forced` and `air.tx_busy_missed`, a deferral only once the burst is armed again at its new instant, and a deferral that could not be armed as a miss, and leave the device in the diagnostics dump's radio group (`core/radio/README.md`).

### The declarations

EN 300 220-1 5.21.3.2 asks the manufacturer to declare seven parameters. They are `PoliteAccess`'s, each held to Table 48 by a `static_assert`, and `test/en300220/` reads them clause by clause.

| 5.21.3.2 | Declared | Table 48 | In the code |
|---|---|---|---|
| 1) the minimum CCA interval | 200 µs | at least 160 µs | `kCcaWindowUs`, the `ChannelLevel` window |
| 2) the maximum dead time | 2 ms | at most 5 ms | `kDeadTimeCeilingUs`, enforced by both executors at the key. The nRF52 executor's reading ends 0.7 to 1 ms before the instant and the bench measured 0.33 ms of keying (`transmit.h`), so 1 to 1.4 ms, not measured on a unit |
| 3) the method that randomises a retransmission | a hash of the device address and the busy instant | | `Transmitter::deferral_draw()` |
| 4) the unit of the deferral period | 5 ms, one ADS-L burst | at least the CCA interval | `kDeferralUnitUs` |
| 5) the minimum and maximum of the random deferral | 5 ms and 40 ms, 1 to 8 units from the busy instant, which `media_access()` never exceeds | | `kDeferralFloorUs`, `kDeferralCeilingUs` |
| 6) Ton max | 5 ms, one 4.64 ms burst | at most 1 s | `kTransmissionCeilingUs` |
| 7) Toff min on the same frequency | 100 ms | at least 100 ms | `kSameFrequencyOffFloorMs`, which the schedule holds (below) |

### What listen-before-talk cannot fix

Carrier sense is what §D.3 asks for, and it does little for the collision it is meant to prevent here. It stays a requirement met, not a protection counted on, for four reasons.

It cannot see the burst it would collide with. A burst is 5 ms and the direct slot 550 ms wide, so two aircraft collide when their drawn instants land within 5 ms of each other: about 1.8% per target in range. Half of those we drew the earlier instant for, and an assessment only ever detects a burst *already* on air. The theoretical ceiling against our own kind is around 0.9% of bursts saved per neighbour.

It listens in the wrong place. The collision happens at the receiver's antenna and the assessment is made at ours. A third aircraft the other side of us hears both bursts collide while we heard nothing, and we defer to a station the intended receiver cannot hear at all. At these ranges the hidden terminal is the normal case, not the corner one.

It is weighted backwards. A burst has to clear -80 dBm to register at all, so the traffic we defer to is the near, loud traffic whose position we already have, while the distant aircraft near the noise floor, the one whose update matters, is never heard and never deferred to.

And it costs the one thing this device exists for. An aircraft that goes quiet is invisible, and a position deferred out of its window is a second without one. What protects the band is the randomised instant, decorrelated by device address and redrawn every second so a collision is not repeated, and the 1% duty cycle this product still declares (below).

The references do it differently. `pjalocha/nrf52-ogn-tracker` (`src/ogn-radio.cpp:840-851`) backs off and escalates, then falls through and transmits when the slot runs out, so its loop has no path that drops the packet. `pjalocha/esp32-ogn-tracker` has the code but both `TimeSlot()` call sites pass `MaxWait=0`, which skips it entirely. Neither SoftRF fork has listen-before-talk at all. This device misses the burst instead, retries it 2 s later, and forces only what §D.3 lets it force.

## The second a burst is dated by

`ClockState::utc_s` is the UTC second that opened at `utc_edge_us`, and `carry_utc_to_edge()` walks it forward on every latched edge. `OwnshipService` re-anchors the pair whenever a solution's own instant is the edge currently latched.

The receiver names a second in a sentence that lands hundreds of milliseconds inside it, so between the edge and that sentence `own.utc_s` is a second behind the edge. Everything that dates a burst against the edge - `radio::stamp_of`, the traffic table's now, the ADS-L and ALP-TAS decoders - read `utc_s` through `bus::State::traffic_now_s()` instead, and a burst drained in that window keeps its own second. Two skyBlips on a bench found this the hard way: one burst read `42:52.954` on the sender and `42:53.956` on the receiver, the same instant, a second apart.

## What the channel measurement is for

`ChannelLevel` is a window of `ChannelLevel::kSamples` instantaneous reads averaged in the linear domain, because the SX1262 has no averaging block and GetRssiInst is an instant by definition (DS 13.5.2). One read lands between two targets' bursts and calls a loud site quiet; a window does not. The same window is the clear channel assessment before a burst (above), and an instrument once per dwell: each executor reads the tuned channel near the dwell's end, and `NoiseFloor` walks that figure into a running average, seeded at OGN's -105 dBm so a cold start reads as quiet rather than as broken. It leaves the device as `noise_dbm` in the status dump.

That number answers a question the counters cannot: a device hearing nothing at a site reading -85 dBm is deaf because the band is full, and one hearing nothing at -110 dBm is deaf for its own reasons. Nothing waits on the floor: only the assessment before a burst gates it.

## The duty cycle is the channel-access route

EN 300 220-2 V3.3.1 Table 4 band M allows "≤ 1 % duty cycle or polite spectrum access", and this product declares the 1% of any hour (`en300220::kBandMDutyCyclePermille`). It listens before talking as well, because ADS-L §D.3 requires it, and does not take the exemption from the duty cycle 4.4.3.1 gives polite access in band M. `AirTime` is the evidence: sixty one-minute buckets on a ring that turns by elapsed time, so the hour that straddles `milliseconds()`'s 49.7-day wrap is an hour like any other.

`Transmitter` asks `AirTime` before every burst and spends it after. At the design rate of one 5 ms position burst per second, a Status and a name every ten seconds (below), we spend 6 ms a second and sit at 0.6 of the allowance, so an empty budget can only be a fault. It blocks: a faulted transmitter that will not stop is worse for everyone on the band than a quiet one. `Attempt::over_budget` says so, and `SlotTimingStatistics::refused()` counts it.

## What the dwell map is for

`kSlot1EndMs` is 1200 ms, 200 ms past the second it opened in, because FLARM-generation traffic is still transmitting there. The position burst stops at 1000 where §C.5 ends the direct slot, and `Transmitter::last_instant_in()` bounds the window each instant is drawn over, so the burst always completes inside both the slot and the dwell that carries it. `first_instant_in()` starts slot 1's window 3 ms into its dwell, so the assessment before a burst listens on the burst's channel and not in the hop.

`SlotPlan::own_tx_dwell` is a property of the dwell, not of the phase the service happens to tick on. Slot 0's dwell opens at 400 and its burst is placed from 450, so the plan that opens the dwell has to carry it.

## The seconds each burst owns

The second alone picks the channel for ADS-L. A position goes on channel A, 868.2 MHz in slot 0, in an even UTC second, and on B, 868.4 MHz in slot 1's direct part, in an odd one, on the ground as well as airborne (`Transmitter::slot_in()`). The Status (§G.2) goes on the other channel from the position of its second, B in an even second and A in an odd one (`extra_slot_in()`). The name always goes on B, in slot 1's tail (below).

Each unit owns a 20 s cycle that starts on an even second, `c = 2 * (serial % 10)` (`cycle_start()`). The serial is the low 16 bits of the address, and skyblip.eu hands serials out densely from 1, so a fleet spreads exactly evenly over ten starting seconds 2 s apart. The address is the unit's own, the one its serial makes, even while it transmits an ICAO address under table 5 (`core/settings/README.md`, The ICAO address): the instants and the deferral draws key on it too, so a pilot who sets or clears the ICAO address mid-flight moves no burst. The start is even because the name has to sit in an even second: there the position is on A, so nothing of ours is on B within 100 ms of the name, and the cycle keeps the name out of the second whose Status is on B. On the ground the cycle holds six bursts, at the same offsets from c for every unit (`kPositionOffsetsS`, `kStatusOffsetsS`, `kCallsignOffsetsS`):

| Offset | Burst | Channel |
|---|---|---|
| +0 | position | A |
| +2 | Status | B |
| +4 | name | B, in the tail, on air 1000 to 1193 ms after second c + 4 starts |
| +9 | position | B |
| +11 | Status | A |
| +14 | name | B, in the tail of c + 14 |

Airborne the position goes every second, and the Status and the name go in the same seconds and on the same channels as on the ground, so a takeoff or a landing changes nothing about them.

A minute for c = 0, A being 868.2 MHz at 450 to 793 ms and B 868.4 MHz at 803 to 993 ms and its tail. The 20 s cycle repeats at 20 and 40:

| Second | Airborne | On the ground |
|---|---|---|
| 0 | position A | position A |
| 2 | position A, Status B | Status B |
| 4 | position A, name B in the tail | name B in the tail |
| 9 | position B | position B |
| 11 | position B, Status A | Status A |
| 14 | position A, name B in the tail | name B in the tail |
| any other | position, A in an even second, B in an odd one | |

On the ground channel A carries 0, 11, 20, 31, 40 and 51, and channel B 2, 5, 9, 15, 22, 25, 29, 35, 42, 45, 49 and 55, where 5, 15, 25 and so on are the names on air in the tail of 4, 14, 24.

Why these offsets, which an exhaustive search over every placement chose:

- With the name pinned to B, channel A carries only a position and a Status per cycle, 11 and 9 s apart, and B carries four bursts, 3, 4, 6 and 7 s apart. Across both channels the bursts are 2, 3, 4, 2, 4 and 5 s apart.
- The parity rule would put two positions 10 s apart on the same channel, which §C.2.5's alternation forbids, so the ground positions are 9 then 11 s apart. That is skyBlip's reading of §G.1.16's "0.1 Hz (once every 10 seconds)": exactly 0.1 Hz on average, the 11 s gap coming from §C.2.5 with the parity rule.
- Positions and Status alternate channel, 9 and 11 s apart, so each channel carries a Status every 20 s and a receiver on one channel only still gets §G.2.11's rate. Names go every 10 s.
- Two bursts of ours on one frequency are always at least 100 ms apart, EN 300 220-1's off-time, the name's tail included, and no dwell carries more than one burst of ours.

The instant inside a window is drawn from the address and the UTC second, so two units that share a second still do not share an instant: the position from `hash(address ^ hash(utc_s))`, the Status from one more round of the hash and the name from two more, so no two bursts share a draw (`Transmitter::extra_instant_in()`). The air time is one position a second airborne plus a Status and a name every ten seconds, 6 ms a second, 0.6 of the allowance, and 0.15% of the hour on the ground. `test/core/test_transmit_cycle.cpp` checks the offsets, the per-channel gaps, the alternation, the one burst a dwell, the 100 ms off-time and the retries over 1024 serials and a whole minute, airborne, on the ground and across a takeoff and a landing.

## A burst that did not go out tries again 2 s later

A burst that did not go out in its second, for any reason (a fix too old, the hour's air time spent, a dwell not armed, a §B.5 hold, a channel busy to the end of the window, the 2000 ms after a forced burst), is retried 2 s later on the same channel (`Transmitter::retry()`, `kRetryStepS`). Two seconds later is the same parity, so the same channel, and §C.2.5's alternation still holds, because the burst that missed was never transmitted. If that second's dwell on the channel already carries a burst of ours, the retry goes to the next same-parity second whose dwell is free, which keeps every dwell to one burst and every frequency's bursts 100 ms apart: the Status at c + 2, whose retry would land on the name's c + 4, goes at c + 6. It keeps trying every 2 s until it goes out or its type's next turn is due, and then that turn is given up and counted in `air.tx_given_up` (`give_up()`). On the ground the position at +0 is retried at +2, +4, +6 and +8, and the one at +9 at +11 to +19. Airborne positions need nothing extra, since their next turn on the same channel is 2 s later anyway.

The suite reads §C.2.5 this way: every transmission the schedule makes alternates channel, and a burst that never went out does not move the next one's channel.

## The tail names us, once every ten seconds

ADS-L carries no callsign. What a pilot is called on the radio reaches the glass and the tablet from OGN's payload type 66 (`core/protocol/README.md`). The name is skyBlip's payload, not ADS-L's, so it goes in slot 1's tail, 1000 to 1193 ms after the start of its second, on 868.4 MHz, where ADS-L transmits nothing today, until ADS-L says where it belongs. §C.5 reserves 0 to 200 ms and lists only Traffic, Status and Remote ID for the direct slot. The name is ours, so it keeps out of the direct slot ADS-L uses and goes in the stretch ADS-L leaves empty.

The instant is drawn over the width the tail leaves once the air time and the completion guard are taken off it, 1000 to 1193 (`last_callsign_instant()`). A name is only worth sending to a receiver that already has a target for us to hang it on, and OGN's `setTargetCall` drops one for an unknown ID (`oss/nrf52-ogn-tracker/src/lookout.h:527-534`). Airborne the target is a second old at most. On the ground the name follows a position by 4 or 5 s, inside the ten seconds any receiver has to keep a ground target to show it at all.

What it costs elsewhere is the flash-write window. `core/timing/durable_write.h` places a settings write inside an armed receive dwell that own-ship cannot transmit in, and slot 1's tail cannot be one, because a name may go out there. The uplink dwell is the one stretch, and it is also the dwell whose traffic we can most afford to be deaf to.

## The next dwell is armed ahead, the burst when it is due

The service loop sleeps 10 ms between passes, so a dwell armed by the pass that noticed its edge opened up to a pass late, and further behind on a pass that ran long. The two captures of 2026-09-27 had slot 1 opening 3 to 6 ms late at the median and up to 120 ms late at worst, and a slot 1 burst drawn for the milliseconds before the arm was never sent. So `RadioService` hands the executor the next dwell as soon as the current one flies, at the absolute instant the latched PPS edge names for it, and the edge between the two is the executor's to keep on its own thread. `ports::Rf::arm()` queues that plan behind the dwell in flight, and the executor switches into it as soon as the flying one ends, never more than `kSwitchLeadMs` ahead of its start, so each guard is spent retuning and the dwell is listening from its first microsecond. The one guard the map names after the dwell it leads to, 200 to 205, is the executor already retuning into the uplink dwell, and the service arms nothing in it.

A burst cannot always be decided that early. Whether one may go out is decided on the fix, the rate and the slot, and those clear when they clear: a solution that lands after 400 ms would cost the whole second if the dwell were armed ahead and never looked at again. So the next dwell is queued bare when its burst is not decided yet, and queued again carrying it the pass its gates clear. And a second plan for the channel the dwell is already flying, carrying a burst that fits inside the window it is already in, is not the next dwell: it is this dwell's burst, and `arm()` hands it to the dwell in flight instead of queueing it. A dwell carries up to two bursts, `ports::RfBursts`, keyed in the order of their instants whichever was armed first, and a burst armed again at an instant the dwell already carries is that burst, not a second one. A burst armed for the queued dwell joins it rather than replacing it, so a burst decided early is not dropped by a plan that arrives later. Every TxDone and every Missed carries the instant its burst was armed for, which is how the tape tells a dwell's two bursts apart. `abort()` cuts the flying dwell short and drops the queued one. A plan that ends inside the flying dwell and cannot join it is refused, never queued: queued, it would have taken the place of the next dwell and then expired. On silicon all of that crosses a thread boundary, published under the scheduler lock; on the host it is the same rule in one thread, which is why the suite can hold it.

The two dwells the executor holds each carry their own burst buffer, because the burst queued on the next dwell is built while the flying one may still be waiting to key. The durable-write policy reads only the flying dwell's burst: a burst queued behind it is not in the air yet.

A window already behind the phase is not armed at all. The guard phases between dwells (`SlotState::Hop`, `SwitchOBandToMBand`) report the dwell that has just closed, and arming that plan produced a stub of a millisecond or two that the executor could only drop. A dropped plan carrying no burst is silent: it is a receive dwell that did not happen, not a transmission that failed.

What each switch costs is the executor's own measurement, from the retune's first command to the SX1262 reporting the new mode, reported by the `timing` command as the worst hop, the worst change to each band, and the dwells armed ahead that were still not listening at their start. Those figures are what the guards should be sized from. The retune itself goes through STDBY_XOSC and falls back there after a burst, because STDBY_RC unpowers the TCXO and the receiver then waits its whole 5 ms start (SX1261/2 datasheet §13.3.6); the hop between the two M-band channels writes the frequency and nothing else.
