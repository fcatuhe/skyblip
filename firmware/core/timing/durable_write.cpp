#include "core/timing/durable_write.h"

#include <algorithm>

namespace skyblip::timing {

namespace {
constexpr int kSecondMs = 1000;
}  // namespace

// INFO: fc 20nov26 a stall on the PPS edge is the worst: it moves every later dwell's anchor
bool DurableWriteWindow::free_at(const SlotPlan& plan, int phase_ms, uint32_t cost_ms) {
    if (plan.tx_allowed) return false;
    if (phase_ms < 0) return false;
    if (phase_ms >= kSecondMs) phase_ms -= kSecondMs;

    // INFO: fc 17nov26 in slot 1's tail, phase, dwell end and next edge count from its own second
    int at_ms = phase_ms;
    if (plan.end_ms > kSecondMs && at_ms < plan.end_ms - kSecondMs) at_ms += kSecondMs;
    const int done_ms = at_ms + static_cast<int>(cost_ms);

    // INFO: fc 17nov26 a write goes inside a running dwell, never in the retune before one
    if (at_ms < plan.start_ms) return false;
    if (done_ms + kSwitchRetuneMs > plan.end_ms) return false;
    // INFO: fc 20nov26 slot 0's dwell opens before the direct slot, where a burst may key at 450
    if (at_ms < adsl::kDirectStartMs && done_ms + kSwitchRetuneMs > adsl::kDirectStartMs)
        return false;
    const int next_edge_ms = at_ms < kSecondMs ? kSecondMs : 2 * kSecondMs;
    return done_ms + kSwitchRetuneMs <= next_edge_ms;
}

void DurableWriteWindow::request(uint32_t now_ms) {
    requests_++;
    if (!pending_) {
        pending_ = true;
        first_request_ms_ = now_ms;
    }
    last_request_ms_ = now_ms;
}

bool DurableWriteWindow::placeable(const SlotPlan& plan, const DwellPhase& dwell, uint32_t now_ms) {
    return free_now(plan, dwell, now_ms, kWorstWriteMs);
}

bool DurableWriteWindow::free_now(const SlotPlan& plan, const DwellPhase& dwell, uint32_t now_ms,
                                  uint32_t cost_ms) {
    if (!dwell.armed) return true;
    if (dwell.burst_armed) return false;
    const uint32_t stale_ms = now_ms - dwell.at_ms;
    if (stale_ms > kViewStaleMs) return false;
    return free_at(plan, dwell.phase_ms + static_cast<int>(stale_ms), cost_ms);
}

DurableWriteVerdict DurableWriteWindow::decide(const SlotPlan& plan, const DwellPhase& dwell,
                                               uint32_t now_ms) const {
    if (!pending_) return DurableWriteVerdict::Idle;
    // INFO: fc 20nov26 the bound outranks the settle: a thumb stepping past it buys a second write
    const bool bound_spent = now_ms - first_request_ms_ >= kDeferCeilingMs;
    if (!bound_spent && now_ms - last_request_ms_ < kSettleMs) return DurableWriteVerdict::Hold;
    if (placeable(plan, dwell, now_ms)) return DurableWriteVerdict::Place;
    return bound_spent ? DurableWriteVerdict::Forced : DurableWriteVerdict::Hold;
}

void DurableWriteWindow::placed(uint32_t now_ms, bool forced) {
    const uint32_t waited_ms = now_ms - first_request_ms_;
    worst_wait_ms_ = std::max(waited_ms, worst_wait_ms_);
    writes_++;
    if (forced) forced_++;
    pending_ = false;
}

void DurableWriteWindow::refused(uint32_t now_ms, bool forced) {
    if (forced) forced_++;
    first_request_ms_ = now_ms;
    last_request_ms_ = now_ms;
}

}  // namespace skyblip::timing
