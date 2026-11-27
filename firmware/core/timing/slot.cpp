#include "core/timing/slot.h"

#include <algorithm>
#include <initializer_list>

namespace skyblip::timing {

namespace {
constexpr uint64_t kSecondUs = 1000000;
constexpr uint64_t kHalfSecondUs = 500000;
constexpr uint32_t kSecondMs = 1000;

struct Dwell {
    SlotState retune;
    SlotState state;
    Band band;
    uint32_t frequency_hz;
    int slot;
    Window window;
};

constexpr Dwell kDwells[] = {
    {SlotState::SwitchMBandToOBand, SlotState::UplinkRxOBand, Band::O, adsl::kOBandHz, -1,
     kUplinkWindowMs},
    {SlotState::SwitchOBandToMBand, SlotState::Slot0, Band::M, adsl::kMBand0Hz, 0, kWindowAMs},
    {SlotState::Hop, SlotState::Slot1, Band::M, adsl::kMBand1Hz, 1, kWindowBMs},
};

// INFO: fc 17nov26 a phase in window B's spill counts from the second its dwell opened in
int unwrapped(int phase_ms) { return phase_ms < kWindowBSpillMs ? phase_ms + 1000 : phase_ms; }

// INFO: fc 17nov26 a dwell owns the phase from the margin before it to the end of its own
const Dwell& dwell_at(int phase_ms) {
    const int at_ms = unwrapped(phase_ms);
    for (const Dwell& dwell : kDwells)
        if (at_ms < dwell_of(dwell.window).end_ms) return dwell;
    return kDwells[2];
}
}  // namespace

void carry_utc_to_edge(ClockState& clock, uint64_t edge_us) {
    clock.pps_edge_us = edge_us;
    if (clock.utc_s == 0 || edge_us <= clock.utc_edge_us) return;
    const uint64_t since_us = edge_us - clock.utc_edge_us;
    const uint32_t seconds = static_cast<uint32_t>((since_us + kHalfSecondUs) / kSecondUs);
    if (seconds == 0) return;
    clock.utc_s += seconds;
    clock.utc_edge_us = edge_us;
}

void take_edge(ClockState& clock, uint64_t edge_us, uint64_t now_us) {
    const bool edged = clock.pps_locked || edge_us != 0;
    clock.since_edge_ms = edged ? static_cast<uint32_t>(since_edge_us(edge_us, now_us) / 1000) : 0;
    carry_utc_to_edge(clock, edge_us);
}

std::optional<UtcReference> utc_reference(const ClockState& clock, uint32_t now_ms) {
    std::optional<UtcReference> edge{};
    if (clock.utc_s != 0 && clock.utc_edge_us != 0)
        edge = UtcReference{clock.utc_s, static_cast<uint32_t>(clock.utc_edge_us / 1000)};
    if (clock.solution_utc_s == 0) return edge;
    const UtcReference solution{clock.solution_utc_s, clock.solution_opened_ms};
    if (!edge || reference_age_ms(solution, now_ms) + kSecondMs <= reference_age_ms(*edge, now_ms))
        return solution;
    return edge;
}

uint32_t reference_age_ms(const UtcReference& reference, uint32_t now_ms) {
    const int32_t age_ms = static_cast<int32_t>(now_ms - reference.opened_ms);
    return age_ms > 0 ? static_cast<uint32_t>(age_ms) : 0;
}

std::optional<UtcReference> claimed_reference(const ClockState& clock, uint32_t now_ms) {
    if (clock.claimed_utc_s == 0) return std::nullopt;
    const UtcReference claimed{clock.claimed_utc_s, clock.claimed_opened_ms};
    if (reference_age_ms(claimed, now_ms) > kUtcReferenceMaxAgeMs) return std::nullopt;
    return claimed;
}

uint32_t carried_utc_s(const UtcReference& reference, uint32_t now_ms) {
    return reference.utc_s + reference_age_ms(reference, now_ms) / kSecondMs;
}

bool in_pps_holdover(const ClockState& clock) {
    return clock.utc_valid && !clock.pps_locked && clock.pps_edge_us != 0 &&
           clock.since_edge_ms <= kPpsHoldoverMs;
}

bool pps_fresh(const ClockState& clock) {
    return clock.pps_locked && clock.since_edge_ms < kPpsEdgeMaxAgeMs;
}

bool pps_anchored(const ClockState& clock) { return clock.pps_locked || in_pps_holdover(clock); }

std::optional<uint64_t> utc_ms(const ClockState& clock) {
    if (!clock.pps_locked || clock.utc_s == 0 || clock.since_edge_ms >= 1000) return std::nullopt;
    return static_cast<uint64_t>(clock.utc_s) * 1000u + clock.since_edge_ms;
}

SlotState Scheduler::state_at(int phase_ms) {
    const Dwell& dwell = dwell_at(phase_ms);
    return unwrapped(phase_ms) < dwell_of(dwell.window).start_ms ? dwell.retune : dwell.state;
}

int Scheduler::slot_of(int phase_ms) { return dwell_at(phase_ms).slot; }

int Scheduler::direct_heard_permille() {
    int heard_ms = 0;
    for (const int slot : {0, 1}) {
        const Window dwell = dwell_of(window(slot));
        heard_ms += std::min(dwell.end_ms, adsl::kDirectEndMs) -
                    std::max(dwell.start_ms, adsl::kDirectStartMs);
    }
    return heard_ms * 1000 / (2 * (adsl::kDirectEndMs - adsl::kDirectStartMs));
}

SlotPlan Scheduler::plan(int phase_ms, const ClockState& clock) {
    phase_ms = std::max(phase_ms, 0) % 1000;
    const Dwell& dwell = dwell_at(phase_ms);
    const Window span = dwell_of(dwell.window);
    const bool anchored = clock.utc_valid && pps_anchored(clock);
    const bool latched = clock.utc_valid && clock.pps_locked;

    SlotPlan slot_plan{};
    slot_plan.state = state_at(phase_ms);
    slot_plan.band = dwell.band;
    slot_plan.frequency_hz = dwell.frequency_hz;
    slot_plan.start_ms = span.start_ms;
    slot_plan.end_ms = span.end_ms;
    slot_plan.tx_allowed = latched && dwell.band == Band::M;
    slot_plan.listen_only = !anchored;
    return slot_plan;
}

}  // namespace skyblip::timing
