# core/timing

One radio, one second. `slot.h` cuts the second into dwells, `transmit.h` decides whether own-ship transmits in one and at which instant, and `channel.h` holds what the channel sounds like and how own-ship shares it. Nothing here touches hardware: `ports::Rf` flies the plan against absolute deadlines.

ADS-L's own rules for a transmitter live in its stack (`protocols/adsl/README.md`). `Transmitter` adds skyBlip's choices, and `RadioService` applies §D.3's listen-before-talk on top.

## The transmit gate

The Go transmits ADS-L only while every requirement below holds. `TransmitGate` (`transmit_gate.h`) judges them once a pass, in `RadioService::judge_transmit()` (`../../products/skyblip_go/services/radio_gate.cpp`), before any burst is decided. Every path that sends a burst or says whether one goes, PFLAU's `TX` field, the glass and the link included, reads that one verdict, `bus::State::transmit`. A failed requirement stops only the bursts. The Go keeps receiving, alarming, feeding the EFB and logging.

| Requirement | What holds it | Reason | ADS-L 4 SRD-860 Issue 2 |
|---|---|---|---|
| an ADS-L address | the serial skyblip.eu signed at activation (`../settings/README.md`) | `ADDRESS` | F.2.2 |
| the radio answering and listening before it talks | in the last 3 s the radio entered a dwell, entered every dwell it was asked to, and read the channel (`kRadioAnswerMaxAgeMs`) | `RADIO` | D.3 |
| the receiver at 2 Hz on 115200, each solution named `.000` or `.500` within 10 ms | `ports::GnssHealth::rate_held`, which the L76K reaches only at 115200, and `gnss::named_quarter_ms()` of the newest solution | `GNSS` | skyBlip's means of meeting G.1.16 inside C.5's direct slot (below) |
| a valid fix, settled | `own.fix_valid`, then `own.tx_settled` (`../gnss/README.md`) | `FIX` | G.1.16 |
| a valid UTC | `own.utc_valid` | `UTC` | C.5, G.1.1 |
| a fresh PPS edge | `pps_fresh()`: a lock, and an edge less than `kPpsEdgeMaxAgeMs`, 1202 ms, old (below) | `PPS` | C.5 |

Issue 2 asks for at least 1 Hz (G.1.16, "should"), and checks each burst on its own: no position older than 500 ms on air (G.1.16) and a Timestamp within 10 ms of the solution (G.1.1). The gate asks 2 Hz because at 1 Hz most of the direct slot is over 490 ms past the solution, so most bursts would be held (The age of a position). The barometer is not a requirement: Issue 2 does not ask for one, and the boot self test refuses a unit without it (`../../products/skyblip_go/product.h`, `kRequired`). The settle after the first fix holds the burst but is never a reason, because it is the normal start.

The reason, `NoTx`, is the first requirement unmet in the table's order. On the pass it fails nothing new is armed, and `RadioService::withdraw_carried()` takes back the bursts already handed to the executor. A burst whose channel the executor is already reading keys or is reported busy, and is not armed again. The bursts resume on the first pass every requirement holds.

`shown()` is the reason the glass marks with `NO TX` on every page, the link's `status` carries as `no_tx`, and the haptic shakes for once in flight (`../../products/skyblip_go/pages/README.md`, NO TX). It appears only after a failure has lasted `kNoTxHoldMs`, 10 s, so a short gap does not flicker it, and stays until every requirement has held for `kNoTxHoldMs`, so a flapping requirement does not shake the pilot every 10 s. The address and the radio are judged from boot, the rest from `kVerdictSettleMs`, 10 s after the first valid fix. `unmet()`, with no hold, is what the `status` page's `TX OFF` and the diagnostics dump name, because the bench wants the instant.

The radio is judged by what its executor reports. A dwell the part would not enter counts in `ports::RfSwitching::refused`. One refusal is a transient bus timeout, and two inside 3 s (`kRadioRefusalSamples`) are a radio not answering. A unit with no address is not asked to listen, so its radio is not judged.

### A burst is timed only from a fresh edge

On a lock every burst keys at most `kPpsEdgeMaxAgeMs`, 1202 ms, after the edge of the second it was planned in, where window B's dwell closes. `pps_fresh()` holds the gate to that age, so C.5 holds through a lost pulse. `Pps` (`../../hardware/platform/zephyr/pps.h`) reads the pulse locked for `kPpsLockTailMs`, 3 s, past its last edge, and that tail carries the dwells and `bus::State::traffic_now_s()`, never a burst. A lost pulse stops the bursts 1.2 s after its last edge and shows `NO TX PPS` 10 s later.

### The PPS holdover keeps the dwells, receive only

Once the edge is stale the Go keeps it for `kPpsHoldoverMs`, 60 s, and only listens. While `pps_anchored()` holds, `RadioService` counts the phase from the held edge, so the dwells stay where the neighbours transmit. `Scheduler::plan()` allows a burst only on a lock, so no holdover dwell carries one. Past the holdover the phase runs free on the uptime clock.

## The burst is drawn, then listened for

`Transmitter::instant_in()` draws the instant from `hash(address ^ hash(utc_s))`, uniform over the burst's window, and the executor listens before it keys there. §D.3 asks for listen-before-talk under EN 300 220-1's Polite Spectrum Access, clause 5.21. The standard's figures are in `protocols/common/en300220.h`, and skyBlip's declarations are `PoliteAccess` in `channel.h`.

The executor reads the burst's channel, already tuned in the dwell, over a 200 us window of nine instant reads averaged as power (`ChannelLevel`), ending at most 2 ms, the dead time, before the burst. Every window opens 3 ms into its dwell, so the 2.2 ms check before a first-instant burst (`PoliteAccess::kCcaLeadUs`) never falls in the retune. At or above the threshold the executor reports `RfEventType::Busy` with the level. A radio found out of receive reports busy with no level.

Both executors start reading 1.2 ms before the instant (`platform::kCcaLeadUs`, `hardware/platform/clear_channel.h`): 0.7 ms of nRF52 staging, the window, and 0.3 ms of read overrun no bench has measured. A burst that would key more than 2 ms after its reading does not key. Once the reading starts the burst belongs to the executor (`RfMailbox::claim()`), and a `withdraw()` is refused.

The threshold is EN 300 220-1 Table 45's for a device under 100 mW e.r.p.: 15 dB above Table 32's limit S = 10 log RB - 117 dBm. With ADS-L's 200 kHz channel, S is -93.99 dBm and the threshold -78.99 dBm at 0 dBd. Table 45's note corrects it by the antenna gain alone. The T-Echo's antenna peaks at 1.6 dBi, -0.55 dBd (`parts::sx1262::kAntennaGainCentiDbd`), so the limit at the connector is -78.44 dBm. The feed loses 0.5 dB to the chip (`kFeedLossCentiDb`), so the chip calls the channel busy from -78.94 dBm, -78 dBm in its whole-dBm reading (`platform::kCcaThresholdCentiDbm`). 200 kHz is the conservative RB: the SX1262's 234.3 kHz filter would allow 0.7 dB more. The gain is the datasheet's, the feed loss an allowance, and the threshold has no margin for the uncalibrated RSSI, all known limits until a bench measures them.

`RadioService` defers a busy burst by a random 3 to 50 units of 5 ms, one ADS-L burst as 5.21.1's note asks (`../../products/skyblip_go/services/radio_access.cpp`). The draw hashes the address and the busy instant (`Transmitter::deferral_draw()`) and counts from that instant, so it spans §C.2's 15 to 250 ms backoff. `adsl::media_access()` draws only from the units left in the burst's window, so a burst with under 15 ms left misses. The burst never moves to another dwell or channel, because §C.2.5 gives the second its channel, so the device takes none of 5.21.4's frequency agility. A deferred position is encoded again from the newest fix. If that fix would be older than 500 ms, the position waits for the next fix and is drawn again, no earlier than the selected backoff permits and still inside its window (The age of a position). A burst missed after a busy assessment is not offered again in that dwell. The retry rule can give it a later dwell.

§D.3 lets a device force a packet 3000 ms after its initial attempt, the turn's first busy assessment (`RadioService::Contention`). skyBlip forces only on the ground, where the next turn is 9 to 11 s away, never airborne, where the 2000 ms of silence after a forced burst would cost two positions. The forced burst carries `RfPlan::tx_forced` and keys without listening. For 2000 ms after it, bursts already handed over are withdrawn (`ports::Rf::withdraw()`) and none is armed.

`air.tx_deferred`, `air.tx_forced` and `air.tx_busy_missed` count deferrals, forced bursts and bursts missed busy (`core/radio/README.md`).

### The declarations

EN 300 220-1 5.21.3.2 asks the manufacturer to declare seven parameters. They are `PoliteAccess`'s, each held to Table 48 by a `static_assert`, and `test/en300220/` reads them clause by clause.

| 5.21.3.2 | Declared | Table 48 | In the code |
|---|---|---|---|
| 1) the minimum CCA interval | 200 us | at least 160 us | `kCcaWindowUs`, the `ChannelLevel` window |
| 2) the maximum dead time | 2 ms | at most 5 ms | `kDeadTimeCeilingUs`, enforced by both executors at the key. About 1 to 1.4 ms on the nRF52, not measured on a unit |
| 3) the method that randomises a retransmission | a hash of the device address and the busy instant | | `Transmitter::deferral_draw()` |
| 4) the unit of the deferral period | 5 ms, one ADS-L burst | at least the CCA interval | `kDeferralUnitUs` |
| 5) the minimum and maximum of the random deferral | 15 ms and 250 ms, 3 to 50 units from the busy instant, ADS-L §C.2's interval, cut where the burst's window ends | | `kDeferralFloorUs`, `kDeferralCeilingUs` |
| 6) Ton max | 5 ms, one 4.64 ms burst | at most 1 s | `kTransmissionCeilingUs` |
| 7) Toff min on the same frequency | 100 ms | at least 100 ms | `kSameFrequencyOffFloorMs`, which the schedule holds (below) |

### What listen-before-talk cannot fix

Carrier sense is a requirement met, not a protection counted on. Two aircraft collide when their 5 ms bursts land together in the 550 ms direct slot, about 1.8% per target in range, and an assessment detects only a burst already on air, so it saves at most half of those. It listens at our antenna, not the receiver's, and hears only near, loud traffic. What protects the band is the randomised instant, redrawn every second so a collision does not repeat, and the 1% duty cycle.

## The age of a position

G.1.1 names the PVT fix's time in the Timestamp to +/-10 ms, and G.1.16 sends no Traffic "containing navigation data older than 500 ms". The L76K solves on `.000` and `.500`, two of the Timestamp's quarters (`../../hardware/parts/l76k/README.md`). A position burst carries the newest solution unextrapolated, stamped with its own quarter, and a solution more than 10 ms off a quarter builds no Traffic (`core/protocol/adsl.cpp`). `own.fix_at_ms` is the solution's instant on the PPS edge (`../gnss/README.md`, PPS, and what it is worth).

The age runs to the instant on air. `Transmitter::attempt()` refuses a position whose solution would then be older than `kFixMaxAgeMs` less `kFixAgeAllowanceMs`: the frame's 5 ms and `kCompletionAllowanceMs`, so the last chip leaves inside 500 ms. A burst keys at its instant or not at all, so it flies at most 490 ms after its solution.

A position whose fix arrives, `own.fix_in_hand_ms`, after its drawn instant less `kRedrawLeadMs` is drawn again: `Transmitter::redrawn_in()` draws over the rest of the window, from the fix plus that lead, keyed on the address and the second so units fed by one receiver model do not bunch. A fix after the window's last instant loses the position. `air.tx_redrawn_for_fix` counts redrawn positions and `air.tx_held_for_fix` lost ones.

A position also needs a fix the clock can date, `own.fix_dated`, mapped through an anchor a valid fix set inside its quarter. Until then it is held like a stale one, and its dwell record says `held_for_fix`.

At 2 Hz, on 868.2 MHz, the instants between 490 ms and the `.500` arrival, about a quarter of the window, move to about 590 to 810 ms. On 868.4 MHz every instant is past 830 ms, so none waits. At 1 Hz only positions drawn into the first 40 ms of slot 0 would go out, so a part that will not take 2 Hz is reset and configured again (`../../hardware/parts/l76k/README.md`, Recovery).

## The second a burst is dated by

`ClockState::utc_s` is the UTC second that opened at `utc_edge_us`, carried forward on every latched edge by `carry_utc_to_edge()`. The receiver names a second hundreds of milliseconds into it, so `own.utc_s` lags the edge until then. Whatever dates a burst against the edge, such as `radio::stamp_of` and the decoders, reads `bus::State::traffic_now_s()` instead.

## What the channel measurement is for

`ChannelLevel` averages `ChannelLevel::kSamples` reads as power, because one GetRssiInst read (DS 13.5.2) between two bursts calls a loud site quiet. Each executor also reads the tuned channel near every dwell's end, and `NoiseFloor` averages it from OGN's -105 dBm seed into `noise_dbm`. A device that hears nothing at -85 dBm is deaf because the band is full, and at -110 dBm for its own reasons. Only the CCA gates a burst.

## The duty cycle is the channel-access route

EN 300 220-2 V3.3.1 Table 4 lets band M use a 1% duty cycle or polite spectrum access. skyBlip declares 1% of any hour (`en300220::kBandMDutyCyclePermille`) and listens too, because §D.3 requires it. `AirTime` keeps sixty one-minute buckets on a ring that turns by elapsed time, so the hour across the 49.7-day wrap counts like any other (`../../ports/README.md`, clock rule 1). At 6 ms a second the device uses 0.6 of the allowance, so an empty budget means a fault, and `Transmitter` then blocks: a faulted transmitter that never stops harms the band more than a quiet one. `Attempt::over_budget` reports it.

## What the dwell map is for

The dwells hear three windows in full: ADS-L's uplink and the two frames skyBlip reads ALP-TAS as sending each second. §C.5 puts the uplink slot at 200 to 450 ms and the direct slot at 450 to 1000 ms. One radio hears one channel at a time, so the uplink gives up its edges.

| Window | Channel | Heard in full | In the code |
|---|---|---|---|
| uplink | O, 869.525 MHz | 210 to 440 ms | `kUplinkWindowMs` |
| window 1 | A, 868.2 MHz | 450 to 820 ms | `kWindowAMs` |
| window 2 | B, 868.4 MHz | 830 to 1200 ms, and it carries the name from 1000 | `kWindowBMs` |

Each M-band window takes one channel, because §C.2.5 alternates traffic, so a receiver that never visits 868.4 MHz hears half its neighbours. Window 2's dwell runs 202 ms into the next second (`kWindowBSpillMs`).

Every switch is the same 10 ms from T, the end of the window before it, and a `static_assert` holds the windows to it:

| From T | What happens | In the code |
|---|---|---|
| +0 to +2 ms | still listening on the old channel, for a late burst | `kSwitchMarginMs` |
| +2 to +7 ms | retune, the same 5 ms for an A to B hop and a band change | `kSwitchRetuneMs` |
| +7 to +10 ms | listening on the new channel, holding the 2.2 ms check before a burst at +10 | `kSwitchLeadMs` |

A dwell is its window plus the lead and the margin (`dwell_of()`). The 5 ms retune covers the bench's worst hop, 1.16 ms over 2880, and no band change has been measured yet.

A burst's instants run from its window's start to the end less its air time and `kCompletionAllowanceMs`, which covers the bench's worst 5.5 ms from SetTx to TxDone (`last_instant_ms()`, `Transmitter::window_of()`):

| Bursts | Instants |
|---|---|
| ADS-L on A | 450 to 810 ms |
| ADS-L on B | 830 to 990 ms, because the direct slot ends at 1000 |
| name on B | 1000 to 1190 ms |
| skyPost uplink | 210 to 425 ms: 440 - 9.4 - 5, rounded down. `kGroundEmitEndMs`, a completion deadline, is 435 |

A ground station may start its 9.4 ms uplink as late as 440.6 ms under §C.5 and end after the uplink dwell closes at 442, which is why skyPost starts from 210. `SlotPlan::tx_allowed` belongs to the dwell, so the plan that opens slot 0's dwell at 447, and the retune before it, carries it.

## The seconds each burst owns

The second alone picks the ADS-L channel. A position goes on A, slot 0, in an even UTC second and on B, slot 1, in an odd one, on the ground and airborne (`Transmitter::slot_in()`). The Status (§G.2) takes the other channel (`extra_slot_in()`). The name always goes on B, in slot 1's tail.

Each unit owns a 20 s cycle from an even second, `c = 2 * (serial % 10)` (`cycle_start()`). skyblip.eu hands serials out densely, so a fleet spreads evenly over the ten start seconds. The address that keys the cycle, the instants and the deferral draws is the unit's own, even while it transmits an ICAO address (`core/settings/README.md`, The ICAO address), so changing that address moves no burst. On the ground the cycle holds six bursts (`kPositionOffsetsS`, `kStatusOffsetsS`, `kCallsignOffsetsS`):

| Offset | Burst | Channel |
|---|---|---|
| +0 | position | A |
| +2 | Status | B |
| +4 | name | B, in the tail, on air 1000 to 1190 ms after second c + 4 starts |
| +9 | position | B |
| +11 | Status | A |
| +14 | name | B, in the tail of c + 14 |

Airborne the position goes every second, and the Status and the name keep their ground seconds. A takeoff or a landing puts two positions in a row on one channel, once, in 45% of the phases the suite walks. Nowhere else does the schedule repeat a channel.

An exhaustive search over every placement chose these offsets:

- The name sits in an even second, where the position is on A, so nothing of ours is on B within 100 ms of it.
- The parity rule would put two ground positions 10 s apart on one channel, which §C.2.5 forbids, so they go 9 then 11 s apart. skyBlip reads §G.1.16's "0.1 Hz (once every 10 seconds)" as that average.
- The odd gap spreads the fleet: across the ten start seconds each of the forty direct windows of the 20 s carries one ground burst. A 10 s gap would put two in a quarter of them and none in another quarter.
- Each channel carries a Status every 20 s, so a single-channel receiver still gets §G.2.11's rate.
- Two bursts of ours on one frequency are at least 100 ms apart, EN 300 220-1's off-time, and no dwell carries two of ours.

The Status's instant takes one more round of the hash and the name's two more, so no two bursts share a draw (`Transmitter::instant_in()`). The ground schedule spends 0.15% of the hour.

## A burst that did not go out tries again 2 s later

A burst that did not go out in its second, for any reason, is retried 2 s later on the same channel (`Transmitter::retry()`, `kRetryStepS`). The same parity keeps the channel, and §C.2.5 still holds because the missed burst was never transmitted. If that dwell already carries a burst of ours, the retry takes the next free same-parity second: the Status at c + 2 retries at c + 6, not on the name's c + 4. Retries continue until the burst goes out or its type's next turn is due, which gives the turn up and counts it in `air.tx_given_up` (`give_up()`). Airborne positions need no retry, since the next turn on the same channel is 2 s later.

## The tail names us, once every ten seconds

ADS-L carries no callsign, so skyBlip sends the name in OGN's payload type 66 (`core/protocol/README.md`). The name is not ADS-L's, so it stays out of the direct slot, which §C.5 lists for Traffic, Status and Remote ID, until ADS-L says where it belongs. It goes in window 2's tail on 868.4 MHz, where ADS-L transmits nothing today. A name helps only a receiver that already holds our target, because OGN's `setTargetCall` drops one for an unknown ID (`oss/nrf52-ogn-tracker/src/lookout.h:527-534`). On the ground it follows a position by 4 or 5 s, inside the ten seconds a receiver keeps a ground target.

## The next dwell is armed ahead, the burst when it is due

`RadioService` hands the executor the next dwell as soon as the current one flies, at the absolute instant the latched edge names, so a long pass cannot open a dwell late. `ports::Rf::arm()` queues it, and the executor switches in at most `kSwitchRetuneMs` before its start, so the dwell listens from its first microsecond.

A burst waits for its gates, such as a young enough fix, so the next dwell is queued bare and queued again with its burst on the pass the gates clear. A plan for the flying dwell's channel and window is that dwell's burst, and `arm()` hands it over. A dwell carries up to two bursts, `ports::RfBursts`, keyed in instant order and named by their instants in TxDone and Missed. A burst armed for the queued dwell joins it, so a later plan never drops it. A plan that ends inside the flying dwell and cannot join it is refused, because queued it would replace the next dwell and expire. The durable-write policy reads only the flying dwell's burst.

Size `kSwitchRetuneMs` from the switch times the `timing` command reports. The retune falls back to STDBY_XOSC, because STDBY_RC unpowers the TCXO and the receiver then waits its 5 ms start (SX1261/2 datasheet §13.3.6).

`SlotTimingStatistics` sorts the PPS interval and each burst's keying error into seven signed buckets. Their edges are `kRetuneUs`, the SX1262's 30 us TS_HOP (DS.SX1261-2.W.APP rev 2.1 §3.5.2 table 3-7), `kSwitchMarginMs` and `kSwitchRetuneMs`, so a bucket says which budget an error broke. A larger error saturates into the outermost bucket, so it never reads as a small one.

## A flash write waits for a receive dwell

While the nRF52840's NVMC writes or erases internal flash, no code runs from it, so the stall reaches the PPS latch and the SX1262's DIO1 (nRF52840 PS v1.8, NVMC). `DurableWriteWindow` (`durable_write.h`) places a write inside an armed receive dwell, a retune's width before its end, clear of any window own-ship may key in and of the next edge. Slot 1's tail may carry the name, so only the uplink dwell is wide enough, and a `static_assert` holds `kWorstWriteMs` to it. The core is deaf to that dwell while it writes, so writes coalesce over `kSettleMs`, and the bound, `kDeferCeilingMs`, runs from the first unwritten change. Past the bound the write is forced and counted as a fault. A partial erase in `kPartialEraseMs` slices keeps even a forced write inside the retune's slack.
