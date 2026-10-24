# Tuning

Every constant the firmware's behavior is tuned by: what the device claims about
itself, what the glass says, what goes on the air, and how often. Generated from
the sources by `scripts/tuning_index.py`, so a value that changes without its
note changing is a red build.

The names read `k<Subject><Mechanism><Unit>`, and
[`firmware/README.md`](../firmware/README.md) is where the mechanisms are
defined: a hold, a settle, a floor and a period are four different things that
all read as "a delay" in conversation.

Out of scope, deliberately: `firmware/hardware/`, `firmware/boards/` and
`firmware/ports/`. Those figures come from datasheets, under the datasheet's
name. Anything a pilot can change is a setting rather than a tuning, and lives
in `firmware/products/skyblip_go/settings.h`.

## [`firmware/core/annunciation`](../firmware/core/annunciation/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kShortestBlipEarCanPlaceMs` | 90 | milliseconds | - | A piezo reaches full amplitude in a couple of milliseconds, so this is an ear figure, not a driver one: shorter than this and a blip is a click that a pilot cannot place, and cannot count. |
| `kTuneSixteenthMs` | `kShortestBlipEarCanPlaceMs` = 90 | milliseconds | - | [README](../firmware/core/annunciation/README.md) argues it |
| `kAdvisorySkyMs` | `2 * kTuneSixteenthMs` = 180 | milliseconds | - | - |
| `kAdvisoryBlipMs` | `kTuneSixteenthMs` = 90 | milliseconds | - | - |
| `kAdvisoryRestMs` | `3 * kTuneSixteenthMs` = 270 | milliseconds | - | - |
| `kAdvisoryLongBlipMs` | `3 * kTuneSixteenthMs` = 270 | milliseconds | - | - |
| `kFirstFixNoteMs` | `kTuneSixteenthMs` = 90 | milliseconds | - | - |
| `kFirstFixNextBeatMs` | `kTuneSixteenthMs` = 90 | milliseconds | - | - |
| `kFirstFixSkipBeatMs` | `3 * kTuneSixteenthMs` = 270 | milliseconds | - | - |
| `kFirstFixHeldNoteMs` | `3 * kTuneSixteenthMs` = 270 | milliseconds | - | - |
| `kReannounceRestMs` | `kTuneSixteenthMs` = 90 | milliseconds | - | The shortest phase any tune asks the service loop to resolve. |
| `kShortestPhaseMs` | `std::min( {shortest_phase_ms(kAdvisoryTune), shortest_phase_ms(kFirstFixTune), kReannounceRestMs})` | milliseconds | - | - |
| `kAnnunciatorOnCeilingMs` | 2000 (2 s) | milliseconds | Ceiling | a guard under the loop, never a tune's: an output still on past it is a bug |

## [`firmware/core/comms`](../firmware/core/comms/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kPromptForgetMs` | `30u * 1000u` = 30000 (30 s) | milliseconds | Forget | A prompt is an open authorisation standing on the glass, so it is not allowed to stand there forever: a device left on a wing with an unanswered "confirm firmware upload" is a device a stranger can walk up to. |
| `kUploadWindowMs` | `10u * 60u * 1000u` = 600000 (10 min) | milliseconds | Window | Long enough to upload ~730 KB over BLE on a slow phone, short enough that a device left on a bench does not stay writable all afternoon. |
| `kDiagnosticsRefreshPeriodMs` | 1000 (1 s) | milliseconds | Period | The snapshot is refreshed once a second and dumped once every ten, which is OGN's own console cadence (oss/nrf52-ogn-tracker src/ogn-radio.cpp:1559-1573). |
| `kDiagnosticsDumpPeriodMs` | 10000 (10 s) | milliseconds | Period | - |
| `kNonceForgetMs` | `10u * 60u * 1000u` = 600000 (10 min) | milliseconds | Forget | #126: long enough to reach the server and back, short of a forgotten tab |

## [`firmware/core/diagnostics`](../firmware/core/diagnostics/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kDutyPeriodCeilingMs` | 60000 (1 min) | milliseconds | Ceiling | a counter crosses the wire as its low half, so 65.536 s of it is the ceiling |
| `kPowerRunRecordPeriodMs` | 30000 (30 s) | milliseconds | Period | 45,220 slots at two records a pass is 188 h, against a 50 h run to cutoff |
| `kStageMarginStepUs` | 10 | microseconds | - | a burst's last byte holds the stage margin in 10 us steps, +-1.27 ms |
| `kMaxRecords` | 64 | records | - | 64 slots: ~30 records/s across a sector erase and the direct slot behind it |

## [`firmware/core/events`](../firmware/core/events/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kStampReachUs` | 2000000 (2000 ms) | microseconds | - | - |

## [`firmware/core/flight`](../firmware/core/flight/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kTurnCeilingDps` | 30 | degrees per second | Ceiling | [README](../firmware/core/flight/README.md) argues it |
| `kClimbWindowFloorMs` | 500 | milliseconds | Floor | Shortest window that still averages out sensor noise, and the longest one whose answer is still "now" rather than a history lesson. |
| `kClimbWindowCeilingMs` | 10000 (10 s) | milliseconds | Ceiling | [README](../firmware/core/flight/README.md) argues it |
| `kExtrapolationCeilingMs` | 1500 (1.5 s) | milliseconds | Ceiling | The model is OGN's, in oss/nrf52-ogn-tracker src/ogn.h:1735-1790: constant ground speed on a constant turn rate, the turn applied as half before the velocity is resolved and half after, plus a constant climb. |
| `kLevelFlightMg` | 1000 | thousandths of g | - | [README](../firmware/core/flight/README.md) argues it |
| `kResultantFloorMg` | 200 | thousandths of g | Floor | [README](../firmware/core/flight/README.md) argues it |
| `kIndicatedStaleMs` | 2000 (2 s) | milliseconds | Stale | [README](../firmware/core/flight/README.md) argues it |
| `kIndicatedDampingSamples` | 4 | samples | Damping | [README](../firmware/core/flight/README.md) argues it |
| `kIndicatedRateDampingMs` | 2000 (2 s) | milliseconds | Damping | [README](../firmware/core/flight/README.md) argues it |
| `kLogStatusPeriodMs` | 60000 (1 min) | milliseconds | Period | the cell and the aircraft heard, once a minute: one record in sixty of the track's |
| `kLogEventBacklogRecords` | 16 | records | - | alarms and encounters waiting on a writer that drains every pass |
| `kTimeOffsetCeilingS` | `0xFFFF` = 65535 | seconds | Ceiling | - |
| `kLogRecordPeriodMs` | 1000 (1 s) | milliseconds | Period | the ADS-L G.1.16 airborne rate, so the log holds what neighbours were told |
| `kLogPreTakeoffRecords` | 60 | records | - | a takeoff is declared at 12 m/s, so the roll is behind the log when it opens |
| `kLogStillHoldMs` | 120000 (2 min) | milliseconds | Hold | a wing that flies at no ground speed has landed once it has stood this long |
| `kSlipFullScaleMg` | 200 | thousandths of g | - | [README](../firmware/core/flight/README.md) argues it |
| `kFlightSpeedMmS` | 12000 (12 m/s) | millimetres per second | - | [README](../firmware/core/flight/README.md) argues it |
| `kLandingSpeedMmS` | 8000 (8 m/s) | millimetres per second | - | [README](../firmware/core/flight/README.md) argues it |
| `kRollingSpeedMmS` | 1500 (1.5 m/s) | millimetres per second | - | [README](../firmware/core/flight/README.md) argues it |
| `kGroundSpeedMmS` | 1000 (1 m/s) | millimetres per second | - | [README](../firmware/core/flight/README.md) argues it |
| `kLandingHoldMs` | 10000 (10 s) | milliseconds | Hold | [README](../firmware/core/flight/README.md) argues it |
| `kAirborneReportPeriodS` | 1 | seconds | Period | [README](../firmware/core/flight/README.md) argues it |
| `kGroundReportPeriodS` | 10 | seconds | Period | [README](../firmware/core/flight/README.md) argues it |
| `kTurnWindowMs` | 1000 (1 s) | milliseconds | Window | [README](../firmware/core/flight/README.md) argues it |

## [`firmware/core/gnss`](../firmware/core/gnss/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kFirstFixSettleMs` | 20000 (20 s) | milliseconds | Settle | the moshe-braner SoftRF fork holds transmission for 20 s after the first fix and 5 s after a re-fix (SoftRF.ino:552-580). |
| `kRefixSettleMs` | 5000 (5 s) | milliseconds | Settle | [README](../firmware/core/gnss/README.md) argues it |
| `kSettleResidualM` | `flight::kFlightSpeedMmS / 1000` = 12 | metres | Settle | the takeoff speed over the second a residual spans, so no smaller error invents a flight |
| `kConvergedFixes` | 3 | fixes | Fixes | - |
| `kSpeedCeilingMmS` | 514444 (514.444 m/s) | millimetres per second | Ceiling | no aircraft flies past COCOM's 1000 kn, the Karman line, or 1 km under the sea |
| `kAltitudeCeilingM` | 100000 | metres | Ceiling | - |
| `kAltitudeFloorM` | -1000 | metres | Floor | - |
| `kSentenceMaxAgeMs` | 3500 (3.5 s) | milliseconds | MaxAge | SoftRF's NMEA_EXP_TIME: liveness, three missed bursts, not a freshness rule |

## [`firmware/core/indication`](../firmware/core/indication/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kShortestFlashEyeCanCatchMs` | 20 | milliseconds | - | An LED reaches full brightness in microseconds, so core/annunciation's 90 ms floor - an ear figure, the shortest blip a pilot can place and count - does not apply. |
| `kShortestPhaseMs` | `shortest_phase_ms()` | milliseconds | - | - |

## [`firmware/core/power`](../firmware/core/power/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kWindowSamples` | 3 | samples | Window | - |
| `kLevelSamples` | 3 | samples | Samples | More than two consecutive samples, so the third one acts. |
| `kRailSettleMs` | 20 | milliseconds | Settle | the 20 ms MB spends between driving the enable pins low and releasing them (nRF52.cpp:2075). |
| `kLongPressHoldMs` | 2000 (2 s) | milliseconds | Hold | Long enough that it cannot be the page press, short enough to do with gloves on. |
| `kParkSettleMs` | 3000 (3 s) | milliseconds | Settle | A power-off parks the panel with a full refresh, which the SSD1681 clocks out in about 2.5 s, and an install or a recovery through black, about a second. |
| `kReleaseSettleMs` | 100 | milliseconds | Settle | nRF52 SENSE is a level detect, not an edge, so arming the wake pin while the button is still down wakes the device the instant SYSTEM OFF latches. |

## [`firmware/core/protocol`](../firmware/core/protocol/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kCycleMs` | `static_cast<int64_t>(kTimestampCycleS) * 1000` | milliseconds | - | - |
| `kAltitudeOffsetM` | 320 | metres | - | - |
| `kTimestampCycleS` | 15 | seconds | - | The timestamp field is quarter seconds inside a 15 s cycle, so the instant a burst claims is expressible to 250 ms - and the position it carries has to belong to that instant rather than to whenever the receiver last spoke. |
| `kTimestampQuarterMs` | 250 | milliseconds | - | - |
| `kUplinkBurstUs` | `kUplinkBurstBits * 1000000u / kUplinkChipRateBps` | microseconds | - | - |
| `kAlptasKeyWindowS` | 18 | seconds | Window | GPS-UTC is 18 s, the widest a receiver holding a fix can be wrong by |
| `kLk8NoAltitudeM` | 99999 | metres | - | - |
| `kLk8NoVarioCmS` | 9999 | seconds | - | - |
| `kLk8AltitudeFloorM` | -1000 | metres | Floor | - |
| `kLk8AltitudeCeilingM` | `kLk8NoAltitudeM - 1` = 99998 | metres | Ceiling | - |
| `kLk8VarioCeilingCmS` | `kLk8NoVarioCmS - 1` = 9998 | seconds | Ceiling | - |
| `kLk8VarioFloorCmS` | `-kLk8VarioCeilingCmS` = -9998 | seconds | Floor | - |

## [`firmware/core/radio`](../firmware/core/radio/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kTxSpanCeilingUs` | 65535 (65.535 ms) | microseconds | Ceiling | the field's own ceiling: a burst this late is a dwell that already ended |

## [`firmware/core/store`](../firmware/core/store/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kSectorEraseCostMs` | 42 | milliseconds | - | MX25R tSE 40 ms typ (prj.conf), +1 ms poll, +commands; 240 max fits no dwell |
| `kSlotWriteCostMs` | 2 | milliseconds | - | one page program on the spi1 NOR, bench-settled, not a datasheet figure |
| `kPageWriteCostMs` | `kSlotWriteCostMs + 1` = 3 | milliseconds | - | one program operation, as the slot write, plus 256 bytes more of SPI at 8 MHz |

## [`firmware/core/timing`](../firmware/core/timing/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kWindowUs` | 160 | microseconds | Window | - |
| `kSamples` | 9 | samples | Samples | [README](../firmware/core/timing/README.md) argues it |
| `kSampleSpacingUs` | `kWindowUs / (kSamples - 1)` = 20 | microseconds | - | - |
| `kMaxSamples` | 16 | samples | Samples | - |
| `kWindowMs` | 3600000 (60 min) | milliseconds | Window | - |
| `kBucketMs` | `kWindowMs / kBuckets` = 514285 (8.57142 min) | milliseconds | - | - |
| `kBudgetMs` | `kWindowMs / 1000 * kLimitPermille` = 36000 (36 s) | milliseconds | - | - |
| `kWordWriteUs` | 41 | microseconds | - | nRF52840 PS v1.8, NVMC chapter, "Electrical specification": tWRITE = 41 us to write one 32-bit word, tERASEPAGE = 85 ms to erase one 4 kB page. |
| `kPageEraseMs` | 85 | milliseconds | - | - |
| `kWorstWriteMs` | `kPageEraseMs + (kBlobWords * kWordWriteUs + 999) / 1000` = 86 | milliseconds | - | - |
| `kPartialEraseMs` | 4 | milliseconds | - | The longest SINGLE stall inside that worst case, which is a different number: |
| `kSettleMs` | 750 | milliseconds | Settle | How long the policy waits for a stream of changes to stop before it writes the one blob they add up to. |
| `kDeferCeilingMs` | 3000 (3 s) | milliseconds | Ceiling | The hard bound, measured from the FIRST unwritten change rather than the last: a setting a pilot changed that is still not on flash when the cell dies is a setting they will believe they changed. |
| `kViewStaleMs` | 100 | milliseconds | Stale | A published view older than this is not evidence about where the second is any more, so it refuses rather than guesses. |
| `kGroundEmitStartMs` | 210 | milliseconds | - | §C.5 gives the second to the bands: 200..450 ms is the uplink slot the ground infrastructure transmits in, 450..1000 the direct slot. |
| `kGroundEmitEndMs` | 390 | milliseconds | - | - |
| `kUplinkRxStartMs` | 205 | milliseconds | - | - |
| `kUplinkRxEndMs` | 395 | milliseconds | - | - |
| `kSlot0StartMs` | 400 | milliseconds | - | - |
| `kSlot0EndMs` | 800 | milliseconds | - | - |
| `kSlot1StartMs` | 800 | milliseconds | - | - |
| `kSlot1EndMs` | 1200 (1.2 s) | milliseconds | - | [README](../firmware/core/timing/README.md) argues it |
| `kSlot1WrapMs` | `kSlot1EndMs - 1000` = 200 | milliseconds | - | How far slot 1 reaches past the second it started in. |
| `kDirectStartMs` | 450 | milliseconds | - | The ADS-L direct slot, which our own bursts respect on top of the dwell map: we listen on M-band from 400 ms, we start transmitting at 450. |
| `kDirectEndMs` | 1000 (1 s) | milliseconds | - | - |
| `kCallsignStartMs` | `kDirectEndMs` = 1000 (1 s) | milliseconds | - | §C.5 reserves 0..200, and §C.2.5's channel 1 is what the dwell is already on |
| `kCallsignEndMs` | `kSlot1EndMs` = 1200 (1.2 s) | milliseconds | - | [README](../firmware/core/timing/README.md) argues it |
| `kJitterGuardMs` | 5 | milliseconds | - | Both band edges get the same guard: slot 1 ends at 200, uplink RX starts at 205. |
| `kHopGuardMs` | 2 | milliseconds | - | bench worst dwell end to listening 1.16 ms over 2880 hops, 2 leaves 0.84 |
| `kSwitchLeadMs` | `kJitterGuardMs` = 5 | milliseconds | - | The executors switch into a queued dwell as soon as the one before it ends, so the guard in front of a dwell is spent retuning and the dwell listens from its start. |
| `kPpsHoldoverMs` | 60000 (1 min) | milliseconds | - | - |
| `kRetuneUs` | 30 | microseconds | - | - |
| `kHopGuardUs` | `kHopGuardMs * 1000` = 2000 (2 ms) | microseconds | - | - |
| `kJitterGuardUs` | `kJitterGuardMs * 1000` = 5000 (5 ms) | microseconds | - | - |
| `kHoldoverGapUs` | 1500000 (1500 ms) | microseconds | - | Less than two nominal seconds, more than any jitter this budget could ever call ordinary: a gap this wide means at least one PPS edge went missing, which is holdover, not a sample for the interval histogram. |
| `kAirTimeMs` | 5 | milliseconds | - | §C.2 at 100 kchip/s: 16 preamble + 64 sync + 24 bytes Manchester, 4.64 ms |
| `kCallsignPeriodS` | 10 | seconds | Period | a name never changes in flight, this only bounds how long a target is hex |
| `kFixMaxAgeMs` | 500 | milliseconds | MaxAge | G.1.16 nav age, to the top of the transmit second: the burst is extrapolated |
| `kCompletionGuardMs` | 2 | milliseconds | - | §C.5: bench worst 5.5 ms, 0.33 keying + 4.94-5.37 SetTx to TxDone, 1.5 left |

## [`firmware/core/traffic`](../firmware/core/traffic/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kAdvisoryAltitudeM` | 300 | metres | - | [README](../firmware/core/traffic/README.md) argues it |
| `kAdvisoryDistanceM` | 3000 | metres | - | [README](../firmware/core/traffic/README.md) argues it |
| `kUnknownTargetSpeedMps` | 30 | metres per second | - | a relayed target arrives with no velocity, and zero would make it the safest dot |
| `kAlertMaxAgeMs` | 5000 (5 s) | milliseconds | MaxAge | SoftRF alerts only on targets seen within ALERT_EXPIRATION_TIME (5 s) and re-checks no more often than every 2 s (oss/SoftRF-lyusupov .../src/TrafficHelper.h:58-59, .../src/TrafficHelper.cpp:236-260). |
| `kRenotifyFloorMs` | 2000 (2 s) | milliseconds | Floor | [README](../firmware/core/traffic/README.md) argues it |
| `kGroundRenotifyFloorMs` | 60000 (1 min) | milliseconds | Floor | a touch-and-go or a stop-and-go is back in the air well inside a minute |
| `kCallsignForgetS` | 600 | seconds | Forget | a name outlives the target it belongs to, which ages out in seconds |
| `kRangeM` | 1000 | metres | - | [README](../firmware/core/traffic/README.md) argues it |
| `kVerticalM` | 100 | metres | - | [README](../firmware/core/traffic/README.md) argues it |
| `kDriftM` | 60 | metres | - | [README](../firmware/core/traffic/README.md) argues it |
| `kVerticalDriftM` | 30 | metres | - | - |
| `kTogetherHoldMs` | 6000 (6 s) | milliseconds | Hold | [README](../firmware/core/traffic/README.md) argues it |
| `kBreakFixes` | 2 | fixes | Fixes | [README](../firmware/core/traffic/README.md) argues it |
| `kClosingMps` | 3 | metres per second | - | [README](../firmware/core/traffic/README.md) argues it |
| `kClosingFixes` | 2 | fixes | Fixes | [README](../firmware/core/traffic/README.md) argues it |
| `kLiftoffWindowMs` | `kTogetherHoldMs` = 6000 (6 s) | milliseconds | Window | [README](../firmware/core/traffic/README.md) argues it |
| `kTargetForgetReports` | 6 | reports of the aircraft's own | Forget | [README](../firmware/core/traffic/README.md) argues it |
| `kAirborneTargetForgetS` | `kTargetForgetReports * flight::kAirborneReportPeriodS` = 6 | seconds | Forget | [README](../firmware/core/traffic/README.md) argues it |
| `kGroundTargetForgetS` | `kTargetForgetReports * flight::kGroundReportPeriodS` = 60 | seconds | Forget | [README](../firmware/core/traffic/README.md) argues it |
| `kPlausibleRangeCeilingM` | 30000 | metres | Ceiling | The honest ceiling of our own link, computed rather than borrowed:  +14 dBm e.r.p. transmitted, which is the ERC 70-03 band h1.4 limit and what the driver programs (hardware/parts/sx1262/sx1262.h); about -107 dBm of receive sensitivity at the M band's 100 kchip/s with the boosted gain of J1 - a bench figure, not a datasheet one; dipole-referenced antennas at both ends and nothing at all in the way. |
| `kRelayedRangeCeilingM` | `2 * kPlausibleRangeCeilingM` = 60000 | metres | Ceiling | A relayed target did not travel that path. |
| `kTurnMaxAgeMs` | 3000 (3 s) | milliseconds | MaxAge | [README](../firmware/core/traffic/README.md) argues it |

## [`firmware/products/skyblip_go`](../firmware/products/skyblip_go/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kRefusalParkCeilingMs` | 6000 (6 s) | milliseconds | Ceiling | a full frame is 2.6 s of BUSY on this panel, and a refusal waits out two |

## [`firmware/products/skyblip_go/input`](../firmware/products/skyblip_go/input/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kLongTouchHoldMs` | 1000 (1 s) | milliseconds | Hold | [README](../firmware/products/skyblip_go/input/README.md) argues it |
| `kDoublePressWindowMs` | 600 | milliseconds | Window | Wider than a human double tap (~200 ms) and far narrower than core/power's kLongPressHoldMs, so the three things the button says stay disjoint: one press pages or refuses, two inside this window authorise, a hold past 2 s powers the device down. |

## [`firmware/products/skyblip_go/pages`](../firmware/products/skyblip_go/pages/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kLevelFlightMg` | `flight::kLevelFlightMg` = 1000 | thousandths of g | - | - |
| `kIdleReturnHoldMs` | 60000 (1 min) | milliseconds | Hold | a menu left open is the traffic picture taken away, and nobody dismissed it |
| `kClimbFigureStepFpm` | 10 | feet per minute | - | [README](../firmware/products/skyblip_go/pages/README.md) argues it |
| `kUptimeClockWrapS` | 10000 | seconds | - | four digits of seconds, so a clock with no UTC behind it still fits its column |
| `kLevelM` | 60 | metres | - | [README](../firmware/products/skyblip_go/pages/README.md) argues it |
| `kLeaderStepMs` | 5000 (5 s) | milliseconds | - | - |
| `kVsiFullScaleFpm` | 2000 | feet per minute | - | - |
| `kVsiKneeFpm` | 1000 | feet per minute | - | - |
| `kVsiMarkFpm` | 500 | feet per minute | - | - |
| `kStandardRateDps` | 3 | degrees per second | - | - |
| `kElapsedCeilingS` | `99 * 60 + 59` = 5999 | seconds | Ceiling | - |

## `firmware/products/skyblip_go/services`

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kPublishPeriodMs` | 1000 (1 s) | milliseconds | Period | the price walks the whole pool, so it is answered at the render cadence |
| `kDrainCeilingRecords` | 16 | records | Ceiling | one pass of slot programs, so the next dwell is armed on time |
| `kAdvertiseRetryPeriodMs` | 1000 (1 s) | milliseconds | Period | a Bluetooth that never came up is asked, and logs its refusal, once a second |
| `kMovingTargetRedrawPeriodMs` | 1000 (1 s) | milliseconds | Period | The cadence is what a moving map needs to draw a target as flying rather than teleporting, and it is not a radio figure. |
| `kTargetRefreshCeilingMs` | `traffic::kAlertMaxAgeMs` = 5000 (5 s) | milliseconds | Ceiling | What the rotation below guarantees: every target in the table reaches the tablet inside this. |
| `kPassDeferralCeilingMs` | 100 | milliseconds | Ceiling | A pass is formatting and a handful of notifications, none of which stalls the core the way a flash write does (core/timing/durable_write.h), but own-ship keying the transmitter is the one window in the second that owes the radio something. |
| `kBarometerClimbWindowMs` | `flight::kClimbWindowFloorMs` = 500 | milliseconds | Window | - |
| `kGnssClimbWindowMs` | 2000 (2 s) | milliseconds | Window | - |
| `kBarometerMaxAgeMs` | `3 * runtime::kBarometerPublishPeriodMs` = 3000 (3 s) | milliseconds | MaxAge | three missed samples: a barometer that went silent hands the climb back |
| `kPpsRecordPeriodMs` | 1000 (1 s) | milliseconds | Period | an edge is a record, and a second that brought none is the record saying so |
| `kMotionRecordPeriodMs` | 1000 (1 s) | milliseconds | Period | the hub reports faster than the filters behind it move, in whole seconds |
| `kDieStaleMs` | 30000 (30 s) | milliseconds | Stale | a sensor that stopped answering must neither hold nor drive the glass |
| `kRecordPeriodMs` | `runtime::kBatteryPeriodMs` = 1000 (1 s) | milliseconds | Period | the cadence the cell is sampled at, so no record repeats a reading |
| `kDutyRecordPeriodMs` | 10000 (10 s) | milliseconds | Period | a counter of screen, receiver and buzzer seconds needs no finer grain |
| `kDiePeriodMs` | 10000 (10 s) | milliseconds | Period | Die temperature moves in minutes: it is the temperature of a lump of plastic in the sun, low-passed by its own mass. |
| `kTxOutcomeMaxAgeMs` | 250 | milliseconds | MaxAge | under the 403 ms between the two M dwells closing, so one retired dwell does |
| `kRenderPeriodMs` | 1000 (1 s) | milliseconds | Period | - |
| `kPresentFloorMs` | 1000 (1 s) | milliseconds | Floor | - |
| `kLockedRedrawWindowStartMs` | 600 | milliseconds | Window | after the fix (330), the drain and slot 0 arming (400); 800+170 ms < 1000 |
| `kLockedRedrawWindowEndMs` | 800 | milliseconds | Window | - |
| `kRecordPeriodMs` | `kRenderPeriodMs` = 1000 (1 s) | milliseconds | Period | the render cadence: a capture says what was on the glass, not what was drawn |

## `firmware/runtime`

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kServicePassCeilingMs` | 10 | milliseconds | Ceiling | - |
| `kServicePassFloorMs` | 2 | milliseconds | Floor | a stream of '\n' or a ringing PPS pin wakes at most 500 passes a second |
| `kServiceSilenceCeilingMs` | 5000 (5 s) | milliseconds | Ceiling | The longest a supervised service may go without reporting progress before the loop stops feeding the dog. |
| `kHardwareWatchdogCeilingMs` | 12000 (12 s) | milliseconds | Ceiling | SoftRF's figure on the same silicon (src/platform/nRF52.cpp:4558). |
| `kRadioRxSilenceCeilingMs` | 30000 (30 s) | milliseconds | Ceiling | - |
| `kBarometerPublishPeriodMs` | 1000 (1 s) | milliseconds | Period | - |
| `kBarometerPpsWindowMs` | `2 * kServicePassCeilingMs` = 20 | milliseconds | Window | - |
| `kBarometerConversionPeriodMs` | 250 | milliseconds | Period | four forced conversions a second feed IIR 4: 0.75 s of lag for 0.1 mA |
| `kBarometerConversionCeilingMs` | 150 | milliseconds | Ceiling | Zephyr's BME280_MEASUREMENT_TIMEOUT_MS, over 3x the DS 9.1 worst case |
| `kBatteryPeriodMs` | 1000 (1 s) | milliseconds | Period | A cell moves over minutes. The gauge needs three readings before it can throw out a transient, so a second between them is the slowest cadence that still shows the state of charge on the first screen a pilot sees. |

185 constants over 18 folders.
