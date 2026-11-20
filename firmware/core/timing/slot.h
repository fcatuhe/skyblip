// INFO: fc 20nov26 the single-radio dwell map of ADS-L 4 SRD-860 issue 2 §C.5, §C.2 and §C.4
#ifndef SKYBLIP_CORE_TIMING_SLOT_H
#define SKYBLIP_CORE_TIMING_SLOT_H

#include <cstdint>
#include <optional>

#include "core/model/band.h"
#include "protocols/adsl/physical.h"
#include "protocols/adsl/transmit.h"

namespace skyblip::timing {

struct Window {
    int start_ms;
    int end_ms;

    constexpr int width_ms() const { return end_ms - start_ms; }
};

// INFO: fc 17nov26 each window is heard in full: the uplink on O, ALP-TAS's two frames on A and B
constexpr Window kUplinkWindowMs = {210, 440};
constexpr Window kWindowAMs = {450, 820};
constexpr Window kWindowBMs = {830, 1200};

// INFO: fc 17nov26 every switch is a margin on the old channel, a retune, a lead on the new one
constexpr int kSwitchMarginMs = 2;
// TODO: fc 17nov26 no band change measured, until a unit's timing command reads its worst one
constexpr int kSwitchRetuneMs = 5;
constexpr int kSwitchLeadMs = 3;
// INFO: fc 29sep26 bench worst 5.5 ms from SetTx to TxDone, against 5 ms booked
constexpr int kCompletionAllowanceMs = 5;

constexpr Window dwell_of(Window window) {
    return {window.start_ms - kSwitchLeadMs, window.end_ms + kSwitchMarginMs};
}

// INFO: fc 17nov26 every burst of ours completes kCompletionAllowanceMs before its window closes
constexpr int last_instant_ms(Window window, uint32_t air_us) {
    return static_cast<int>((int64_t{window.end_ms} * 1000 - air_us) / 1000) -
           kCompletionAllowanceMs;
}

constexpr int kGroundEmitStartMs = kUplinkWindowMs.start_ms;
// INFO: fc 05aug26 a completion deadline, not a start deadline
constexpr int kGroundEmitEndMs = kUplinkWindowMs.end_ms - kCompletionAllowanceMs;
constexpr int kWindowBSpillMs = dwell_of(kWindowBMs).end_ms - 1000;

constexpr bool switched(Window before, Window after) {
    return (after.start_ms - before.end_ms + 1000) % 1000 ==
           kSwitchMarginMs + kSwitchRetuneMs + kSwitchLeadMs;
}
static_assert(switched(kUplinkWindowMs, kWindowAMs) && switched(kWindowAMs, kWindowBMs) &&
                  switched(kWindowBMs, kUplinkWindowMs),
              "every switch between two windows is the same one");

constexpr uint32_t kPpsHoldoverMs = 60000;
// INFO: fc 20nov26 a pulse reads locked this long past its last edge, so one missed edge keeps it
constexpr uint32_t kPpsLockTailMs = 3000;
// INFO: fc 20nov26 C.5: the latest instant a burst planned from an edge keys at, window B's close
constexpr uint32_t kPpsEdgeMaxAgeMs = static_cast<uint32_t>(dwell_of(kWindowBMs).end_ms);

using model::Band;

// INFO: fc 17nov26 a switch state is the retune into the next dwell, and plans that dwell
enum class SlotState : uint8_t {
    UplinkRxOBand = 0,
    SwitchOBandToMBand = 1,
    Slot0 = 2,
    Hop = 3,
    Slot1 = 4,
    SwitchMBandToOBand = 5,
};

struct ClockState {
    bool utc_valid{false};
    bool pps_locked{false};
    uint32_t since_edge_ms{0};
    // INFO: fc 17nov26 the latched edge on the deadline clock, so a phase is derived where used
    uint64_t pps_edge_us{0};
    // INFO: fc 16sep26 the second that opened at utc_edge_us, not the one the last sentence named
    uint32_t utc_s{0};
    uint64_t utc_edge_us{0};
    // INFO: fc 18nov26 the anchor came from a fix inside its quarter, so positions trust it
    bool utc_anchor_fixed{false};
};

void carry_utc_to_edge(ClockState& clock, uint64_t edge_us);

// INFO: fc 19nov26 one edge read dates the second carried and the time since it
void take_edge(ClockState& clock, uint64_t edge_us, uint64_t now_us);

// INFO: fc 28sep26 the edge interrupt can land between reading now and reading the edge
constexpr uint64_t since_edge_us(uint64_t edge_us, uint64_t now_us) {
    return now_us > edge_us ? now_us - edge_us : 0;
}

// INFO: fc 19nov26 unlocked since an edge recent enough that the slot map still flies on it
bool in_pps_holdover(const ClockState& clock);

// INFO: fc 20nov26 the dwells run on the latched edge, or the last one held, which keys no burst
bool pps_anchored(const ClockState& clock);

// INFO: fc 20nov26 a burst is timed only from an edge as recent as a locked second's
bool pps_fresh(const ClockState& clock);

// INFO: fc 12nov26 UTC to the millisecond, none without a PPS lock and a dated second
std::optional<uint64_t> utc_ms(const ClockState& clock);

struct SlotPlan {
    SlotState state{SlotState::UplinkRxOBand};
    Band band{Band::O};
    uint32_t frequency_hz{adsl::kOBandHz};
    // INFO: fc 17nov26 dwell_of() the window, the retune before it is the executor's
    int start_ms{0};
    int end_ms{0};
    bool tx_allowed{false};
    bool listen_only{true};
};

class Scheduler {
   public:
    static SlotPlan plan(int phase_ms, const ClockState& clock);

    static SlotState state_at(int phase_ms);
    static bool in_direct_slot(int phase_ms) {
        return phase_ms >= adsl::kDirectStartMs && phase_ms < adsl::kDirectEndMs;
    }
    // INFO: fc 17nov26 the M-band channel a phase plans, the retune into it included, else -1
    static int slot_of(int phase_ms);
    static uint32_t slot_frequency_hz(int slot) { return adsl::channel_hz(slot); }
    static constexpr Window window(int slot) { return slot == 1 ? kWindowBMs : kWindowAMs; }
    // INFO: fc 13nov26 G.2.2: the share of the direct slot heard, averaged over both channels
    static int direct_heard_permille();
};

}  // namespace skyblip::timing

#endif
