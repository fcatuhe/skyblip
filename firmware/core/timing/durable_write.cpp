#include "core/timing/durable_write.h"

#include <algorithm>

namespace skyblip::timing {

namespace {
constexpr int kSecondMs = 1000;
}  // namespace

// Four refusals, in the order they cost. The first is the loudest: an armed dwell
// hands the radio a plan, and the CPU's remaining debt to it is (a) the instant it
// must be re-armed, (b) the instant own-ship keys the PA inside it, and (c) the
// PPS edge the next second's deadlines are all measured from. A stall that lands
// on the third is the worst of the three, because it does not delay one dwell -
// it moves the anchor under every dwell that follows.
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
    // §C.5's direct slot is narrower than the M-band dwells at both ends, so a
    // write placed in slot 0's head would still be holding the core at 450 ms,
    // which is an instant core/timing::Transmitter is allowed to have chosen.
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
    // Nothing is armed: a product with no radio fitted, or one before the first
    // dwell. There is no second to respect.
    if (!dwell.armed) return true;
    // A burst is on the plan and its outcome has not been reported. That dwell is
    // the one the whole guard exists for, whatever phase it is at.
    if (dwell.burst_armed) return false;
    const uint32_t stale_ms = now_ms - dwell.at_ms;
    if (stale_ms > kViewStaleMs) return false;
    return free_at(plan, dwell.phase_ms + static_cast<int>(stale_ms), cost_ms);
}

DurableWriteVerdict DurableWriteWindow::decide(const SlotPlan& plan, const DwellPhase& dwell,
                                               uint32_t now_ms) const {
    if (!pending_) return DurableWriteVerdict::Idle;
    // The bound outranks the coalescing settle, and then the window: a thumb that
    // keeps stepping a value for longer than the bound buys a second write, which
    // is the trade this way round on purpose.
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
