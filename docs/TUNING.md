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

## `firmware/core/annunciation`

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kShortestBlipEarCanPlaceMs` | 90 | milliseconds | - | A piezo reaches full amplitude in a couple of milliseconds, so this is an ear figure, not a driver one: shorter than this and a blip is a click that a pilot cannot place, and cannot count. |
| `kAdvisoryPairBeepMs` | 250 | milliseconds | - | - |
| `kAdvisoryPairGapSameAsBeepMs` | 250 | milliseconds | - | - |
| `kFirstFixNoteMs` | `kShortestBlipEarCanPlaceMs` = 90 | milliseconds | - | - |
| `kFirstFixBeatMs` | `2 * kFirstFixNoteMs` = 180 | milliseconds | - | - |
| `kFirstFixNextBeatMs` | `kFirstFixBeatMs - kFirstFixNoteMs` = 90 | milliseconds | - | - |
| `kFirstFixSkipBeatMs` | `kFirstFixNextBeatMs + kFirstFixBeatMs` = 270 | milliseconds | - | - |
| `kFirstFixHeldNoteMs` | `kFirstFixBeatMs + kFirstFixNextBeatMs` = 270 | milliseconds | - | - |
| `kShortestPhaseMs` | `kShortestBlipEarCanPlaceMs` = 90 | milliseconds | - | The shortest phase any pattern asks the service loop to resolve. |

## [`firmware/core/comms`](../firmware/core/comms/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kConfirmWindowMs` | `30u * 1000u` = 30000 (30 s) | milliseconds | Window | A prompt is an open authorisation standing on the glass, so it is not allowed to stand there forever: a device left on a wing with an unanswered "confirm firmware upload" is a device a stranger can walk up to. |
| `kUploadWindowMs` | `10u * 60u * 1000u` = 600000 (10 min) | milliseconds | Window | Long enough to upload ~730 KB over BLE on a slow phone, short enough that a device left on a bench does not stay writable all afternoon. |
| `kDiagnosticsRefreshMs` | 1000 (1 s) | milliseconds | - | The snapshot is refreshed once a second and dumped once every ten, which is OGN's own console cadence (oss/nrf52-ogn-tracker src/ogn-radio.cpp:1559-1573). |
| `kDiagnosticsDumpMs` | 10000 (10 s) | milliseconds | - | - |

## [`firmware/core/events`](../firmware/core/events/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kStampReachUs` | 2000000 (2000 ms) | microseconds | - | - |

## [`firmware/core/flight`](../firmware/core/flight/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kMaxTurnDps` | 30 | degrees per second | - | [README](../firmware/core/flight/README.md) argues it |
| `kMinWindowMs` | 500 | milliseconds | Window | Shortest window that still averages out sensor noise, and the longest one whose answer is still "now" rather than a history lesson. |
| `kMaxWindowMs` | 10000 (10 s) | milliseconds | Window | [README](../firmware/core/flight/README.md) argues it |
| `kMaxExtrapolationMs` | 1500 (1.5 s) | milliseconds | - | The model is OGN's, in oss/nrf52-ogn-tracker src/ogn.h:1735-1790: constant ground speed on a constant turn rate, the turn applied as half before the velocity is resolved and half after, plus a constant climb. |
| `kLevelFlightMg` | 1000 | thousandths of g | - | [README](../firmware/core/flight/README.md) argues it |
| `kResultantFloorMg` | 200 | thousandths of g | Floor | [README](../firmware/core/flight/README.md) argues it |
| `kIndicatedStaleMs` | 2000 (2 s) | milliseconds | Stale | [README](../firmware/core/flight/README.md) argues it |
| `kIndicatedSamples` | 8 | samples | Samples | [README](../firmware/core/flight/README.md) argues it |
| `kMaxTimeOffsetS` | `0xFFFF` = 65535 | seconds | - | - |
| `kLogRecordPeriodMs` | 4000 (4 s) | milliseconds | Period | Four seconds. It is the interval the moshe-braner SoftRF fork ships as its default (oss/SoftRF-moshe-braner .../src/driver/ Settings.cpp:838 loginterval = 4) and it is inside every fix-interval a badge, an OLC claim or a competition file is scored on. |
| `kLogPreTakeoffRecords` | 8 | records | - | a takeoff is declared at 12 m/s, so the roll is behind the log when it opens |
| `kSlipFullScaleMg` | 200 | thousandths of g | - | [README](../firmware/core/flight/README.md) argues it |
| `kFlightSpeedMmS` | 12000 (12 m/s) | millimetres per second | - | [README](../firmware/core/flight/README.md) argues it |
| `kLandingSpeedMmS` | 8000 (8 m/s) | millimetres per second | - | [README](../firmware/core/flight/README.md) argues it |
| `kTaxiSpeedMmS` | 1500 (1.5 m/s) | millimetres per second | - | [README](../firmware/core/flight/README.md) argues it |
| `kGroundSpeedMmS` | 1000 (1 m/s) | millimetres per second | - | [README](../firmware/core/flight/README.md) argues it |
| `kLandingHoldMs` | 10000 (10 s) | milliseconds | Hold | [README](../firmware/core/flight/README.md) argues it |
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
| `kWindowSamples` | 3 | samples | Samples | - |
| `kCutoffSamples` | 3 | samples | Samples | More than two consecutive samples, so the third one acts. |
| `kRailSettleMs` | 20 | milliseconds | Settle | the 20 ms MB spends between driving the enable pins low and releasing them (nRF52.cpp:2075). |
| `kLongPressMs` | 2000 (2 s) | milliseconds | - | Long enough that it cannot be the page press, short enough to do with gloves on. |
| `kParkMs` | 3000 (3 s) | milliseconds | - | The panel is parked with a full refresh and the SSD1681 clocks one out in about 2.5 s. |
| `kReleaseSettleMs` | 100 | milliseconds | Settle | nRF52 SENSE is a level detect, not an edge, so arming the wake pin while the button is still down wakes the device the instant SYSTEM OFF latches. |
| `kPlateauHoldMs` | 120000 (2 min) | milliseconds | Hold | [README](../firmware/core/power/README.md) argues it |

## [`firmware/core/protocol`](../firmware/core/protocol/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kCycleMs` | `static_cast<int64_t>(kTimeStampCycleS) * 1000` | milliseconds | - | - |
| `kAltOffsetM` | 320 | metres | - | - |
| `kTimeStampCycleS` | 15 | seconds | - | The TimeStamp field is quarter seconds inside a 15 s cycle, so the instant a burst claims is expressible to 250 ms - and the position it carries has to belong to that instant rather than to whenever the receiver last spoke. |
| `kTimeStampQuarterMs` | 250 | milliseconds | - | - |
| `kUplinkBurstUs` | `kUplinkBurstBits * 1000000u / kUplinkChipRateBps` | microseconds | - | - |
| `kAlptasKeyWindowS` | 18 | seconds | Window | GPS-UTC is 18 s, the widest a receiver holding a fix can be wrong by |
| `kLk8NoAltitudeM` | 99999 | metres | - | - |
| `kLk8NoVarioCmS` | 9999 | seconds | - | - |
| `kLk8MinAltitudeM` | -1000 | metres | - | - |
| `kLk8MaxAltitudeM` | `kLk8NoAltitudeM - 1` = 99998 | metres | - | - |
| `kLk8MaxVarioCmS` | `kLk8NoVarioCmS - 1` = 9998 | seconds | - | - |
| `kLk8MinVarioCmS` | `-kLk8MaxVarioCmS` = -9998 | seconds | - | - |

## [`firmware/core/radio`](../firmware/core/radio/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kTxSpanLimitUs` | 65535 (65.535 ms) | microseconds | - | the field's own ceiling: a burst this late is a dwell that already ended |

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
| `kMaxDeferMs` | 3000 (3 s) | milliseconds | - | The hard bound, measured from the FIRST unwritten change rather than the last: a setting a pilot changed that is still not on flash when the cell dies is a setting they will believe they changed. |
| `kViewStaleMs` | 100 | milliseconds | Stale | A published view older than this is not evidence about where the second is any more, so it refuses rather than guesses. |
| `kJitterGuardMs` | 5 | milliseconds | - | Both band edges get the same guard: slot 1 ends at 200, uplink RX starts at 205. |
| `kHopGuardMs` | 1 | milliseconds | - | - |
| `kPpsHoldoverMs` | 60000 (1 min) | milliseconds | - | - |
| `kRetuneUs` | 30 | microseconds | - | - |
| `kHopGuardUs` | `kHopGuardMs * 1000` = 1000 (1 ms) | microseconds | - | - |
| `kJitterGuardUs` | `kJitterGuardMs * 1000` = 5000 (5 ms) | microseconds | - | - |
| `kHoldoverGapUs` | 1500000 (1500 ms) | microseconds | - | Less than two nominal seconds, more than any jitter this budget could ever call ordinary: a gap this wide means at least one PPS edge went missing, which is holdover, not a sample for the interval histogram. |
| `kAirTimeMs` | 5 | milliseconds | - | §C.2 at 100 kchip/s: 16-chip preamble, 64-chip Manchester sync word, then 25 Manchester-encoded bytes = 4.8 ms, rounded up. |
| `kGroundPeriodS` | 10 | seconds | Period | §G.1.16: at least 1 Hz airborne, 0.1 Hz on the ground. |
| `kAirbornePeriodS` | 1 | seconds | Period | - |
| `kCallsignPeriodS` | 10 | seconds | Period | a name never changes in flight, this only bounds how long a contact is hex |
| `kFixLagMaxMs` | 500 | milliseconds | - | G.1.16 nav age, to the top of the transmit second: the burst is extrapolated |
| `kCompletionSlackMs` | 5 | milliseconds | - | Ours, not the spec's: §C.5 gives the direct slot 450..1000 and requires a burst to complete before the slot ends. |

## [`firmware/core/traffic`](../firmware/core/traffic/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kAdvisoryAltM` | 300 | metres | - | [README](../firmware/core/traffic/README.md) argues it |
| `kAdvisoryDistM` | 3000 | metres | - | [README](../firmware/core/traffic/README.md) argues it |
| `kUnknownTargetSpeedMps` | 30 | metres per second | - | a relayed target arrives with no velocity, and zero would make it the safest dot |
| `kAlertMaxAgeMs` | 5000 (5 s) | milliseconds | MaxAge | SoftRF alerts only on targets seen within ALERT_EXPIRATION_TIME (5 s) and re-checks no more often than every 2 s (oss/SoftRF-lyusupov .../src/TrafficHelper.h:58-59, .../src/TrafficHelper.cpp:236-260). |
| `kRenotifyFloorMs` | 2000 (2 s) | milliseconds | Floor | [README](../firmware/core/traffic/README.md) argues it |
| `kTargetForgetMs` | 12000 (12 s) | milliseconds | Forget | [README](../firmware/core/traffic/README.md) argues it |
| `kCallsignForgetS` | 600 | seconds | Forget | a name outlives the target it belongs to, which ages out in seconds |
| `kRangeM` | 1000 | metres | - | [README](../firmware/core/traffic/README.md) argues it |
| `kVertM` | 100 | metres | - | [README](../firmware/core/traffic/README.md) argues it |
| `kDriftM` | 60 | metres | - | [README](../firmware/core/traffic/README.md) argues it |
| `kVertDriftM` | 30 | metres | - | - |
| `kTogetherHoldMs` | 6000 (6 s) | milliseconds | Hold | [README](../firmware/core/traffic/README.md) argues it |
| `kBreakFixes` | 2 | fixes | Fixes | - |
| `kContactForgetMs` | 12000 (12 s) | milliseconds | Forget | [README](../firmware/core/traffic/README.md) argues it |
| `kClosingMps` | 3 | metres per second | - | [README](../firmware/core/traffic/README.md) argues it |
| `kMaxPlausibleRangeM` | 30000 | metres | - | The honest ceiling of our own link, computed rather than borrowed:  +14 dBm e.r.p. transmitted, which is the ERC 70-03 band h1.4 limit and what the driver programs (hardware/parts/sx1262/sx1262.h); about -107 dBm of receive sensitivity at the M band's 100 kchip/s with the boosted gain of J1 - a bench figure, not a datasheet one; dipole-referenced antennas at both ends and nothing at all in the way. |
| `kMaxRelayedRangeM` | `2 * kMaxPlausibleRangeM` = 60000 | metres | - | A relayed target did not travel that path. |
| `kTurnMaxGapMs` | 3000 (3 s) | milliseconds | - | [README](../firmware/core/traffic/README.md) argues it |
| `kDirectPreferredMaxAgeSec` | 5 | seconds | MaxAge | How long a first-hand reception keeps a target to itself before a ground relay of the same aircraft is allowed to refresh it. |
| `kDefaultMaxAgeSec` | 12 | seconds | MaxAge | [README](../firmware/core/traffic/README.md) argues it |

## [`firmware/products/skyblip_go`](../firmware/products/skyblip_go/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kRefusalParkCeilingMs` | 6000 (6 s) | milliseconds | Ceiling | a full frame is 2.6 s of BUSY on this panel, and a refusal waits out two |

## [`firmware/products/skyblip_go/input`](../firmware/products/skyblip_go/input/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kLongTouchMs` | 1000 (1 s) | milliseconds | - | [README](../firmware/products/skyblip_go/input/README.md) argues it |
| `kDoublePressMs` | 600 | milliseconds | - | Wider than a human double tap (~200 ms) and far narrower than core/power's kLongPressMs, so the three things the button says stay disjoint: one press pages or refuses, two inside this window authorise, a hold past 2 s powers the device down. |

## [`firmware/products/skyblip_go/pages`](../firmware/products/skyblip_go/pages/README.md)

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kLevelFlightMg` | `flight::kLevelFlightMg` = 1000 | thousandths of g | - | - |
| `kIdleReturnMs` | 60000 (1 min) | milliseconds | - | a menu left open is the traffic picture taken away, and nobody dismissed it |
| `kUptimeClockWrapS` | 10000 | seconds | - | four digits of seconds, so a clock with no UTC behind it still fits its column |
| `kLevelM` | 60 | metres | - | [README](../firmware/products/skyblip_go/pages/README.md) argues it |
| `kLeaderStepMs` | 5000 (5 s) | milliseconds | - | - |
| `kStandardRateDps` | 3 | degrees per second | - | - |
| `kSlipFullMg` | `flight::kSlipFullScaleMg` = 200 | thousandths of g | - | - |

## `firmware/products/skyblip_go/services`

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kHapticFeltThroughAHarnessMs` | 400 | milliseconds | - | - |
| `kPublishPeriodMs` | 1000 (1 s) | milliseconds | Period | the price walks the whole pool, so it is answered at the render cadence |
| `kDrainCeilingRecords` | 16 | records | Ceiling | one pass of slot programs, so the next dwell is armed on time |
| `kMovingTargetRedrawMs` | 1000 (1 s) | milliseconds | - | The cadence is what a moving map needs to draw a target as flying rather than teleporting, and it is not a radio figure. |
| `kTargetRefreshBoundMs` | `traffic::kAlertMaxAgeMs` = 5000 (5 s) | milliseconds | - | What the rotation below guarantees: every target in the table reaches the tablet inside this. |
| `kPassDeferralCeilingMs` | 100 | milliseconds | Ceiling | A pass is formatting and a handful of notifications, none of which stalls the core the way a flash write does (core/timing/durable_write.h), but own-ship keying the transmitter is the one window in the second that owes the radio something. |
| `kBaroVsWindowMs` | `flight::kMinWindowMs` = 500 | milliseconds | Window | - |
| `kGnssVsWindowMs` | 2000 (2 s) | milliseconds | Window | - |
| `kBaroMaxAgeMs` | `3 * runtime::kBaroPeriodMs` = 3000 (3 s) | milliseconds | MaxAge | three missed samples: a barometer that went silent hands the climb back |
| `kPpsRecordPeriodMs` | 1000 (1 s) | milliseconds | Period | an edge is a record, and a second that brought none is the record saying so |
| `kMotionRecordPeriodMs` | 1000 (1 s) | milliseconds | Period | the hub reports faster than the filters behind it move, in whole seconds |
| `kDieStaleMs` | 30000 (30 s) | milliseconds | Stale | a sensor that stopped answering must neither hold nor drive the glass |
| `kRecordPeriodMs` | `runtime::kBatteryPeriodMs` = 1000 (1 s) | milliseconds | Period | the cadence the cell is sampled at, so no record repeats a reading |
| `kDiePeriodMs` | 10000 (10 s) | milliseconds | Period | Die temperature moves in minutes: it is the temperature of a lump of plastic in the sun, low-passed by its own mass. |
| `kSectorEraseCostMs` | 40 | milliseconds | - | budgets for the external NOR on spi1, bench-settled, not datasheet figures |
| `kSlotWriteCostMs` | 2 | milliseconds | - | - |
| `kPpsEdgeMissedMs` | 1500 (1.5 s) | milliseconds | - | one edge a second, so a phase older than this is an edge that never came |
| `kRenderPeriodMs` | 1000 (1 s) | milliseconds | Period | - |
| `kPresentFloorMs` | 1000 (1 s) | milliseconds | Floor | - |
| `kRecordPeriodMs` | `kRenderPeriodMs` = 1000 (1 s) | milliseconds | Period | the render cadence: a capture says what was on the glass, not what was drawn |

## `firmware/runtime`

| Constant | Value | Unit | Mechanism | Why |
|---|---|---|---|---|
| `kServiceStepMs` | 10 | milliseconds | - | - |
| `kTaskWatchdogMs` | 5000 (5 s) | milliseconds | - | The longest a supervised service may go without reporting progress before the loop stops feeding the dog. |
| `kHardwareWatchdogMs` | 12000 (12 s) | milliseconds | - | SoftRF's figure on the same silicon (src/platform/nRF52.cpp:4558). |
| `kRadioNoRxReinitMs` | 30000 (30 s) | milliseconds | - | - |
| `kPpsLossListenOnlyMs` | 60000 (1 min) | milliseconds | - | - |
| `kBaroPeriodMs` | 1000 (1 s) | milliseconds | Period | - |
| `kBaroPpsWindowMs` | `2 * kServiceStepMs` = 20 | milliseconds | Window | - |
| `kBatteryPeriodMs` | 1000 (1 s) | milliseconds | Period | A cell moves over minutes. The gauge needs three readings before it can throw out a transient, so a second between them is the slowest cadence that still shows the state of charge on the first screen a pilot sees. |

147 constants over 16 folders.
