// One radio, one second, two bands: the dwell map decides what the receiver can
// hear and when it is allowed to speak. These pin down both halves. A dwell that
// runs past its edge or a burst that starts too late to finish inside the direct
// slot transmits into someone else's window, and a device that keeps transmitting
// once UTC is gone does it blind. Without a clock the answer is listen only.
#include "core/timing/slot.h"
#include "doctest/doctest.h"
#include "test/support/anchored_clock.h"

using namespace skyblip;
using namespace skyblip::timing;

constexpr uint64_t kLastEdgeUs = 12'000'000;

// The owner's rule of 17nov26: three windows heard in full, one switch between each two.
TEST_CASE("timing: the dwell map covers the uplink, ALP-TAS's first window and its second") {
    CHECK(kUplinkWindowMs.start_ms == 210);
    CHECK(kUplinkWindowMs.end_ms == 440);
    CHECK(kWindowAMs.start_ms == 450);
    CHECK(kWindowAMs.end_ms == 820);
    CHECK(kWindowBMs.start_ms == 830);
    CHECK(kWindowBMs.end_ms == 1200);
}

TEST_CASE("timing: every switch is 2 ms on the old channel, a 5 ms retune, 3 ms on the new one") {
    CHECK(kSwitchMarginMs == 2);
    CHECK(kSwitchRetuneMs == 5);
    CHECK(kSwitchLeadMs == 3);
    struct Switch {
        int from_ms;
        SlotState before;
        SlotState retune;
        SlotState after;
    };
    const Switch switches[] = {
        {kWindowBMs.end_ms - 1000, SlotState::Slot1, SlotState::SwitchMBandToOBand,
         SlotState::UplinkRxOBand},
        {kUplinkWindowMs.end_ms, SlotState::UplinkRxOBand, SlotState::SwitchOBandToMBand,
         SlotState::Slot0},
        {kWindowAMs.end_ms, SlotState::Slot0, SlotState::Hop, SlotState::Slot1},
    };
    for (const Switch& at : switches) {
        CAPTURE(at.from_ms);
        CHECK(Scheduler::state_at(at.from_ms - 1) == at.before);
        CHECK(Scheduler::state_at(at.from_ms + 1) == at.before);
        CHECK(Scheduler::state_at(at.from_ms + 2) == at.retune);
        CHECK(Scheduler::state_at(at.from_ms + 6) == at.retune);
        CHECK(Scheduler::state_at(at.from_ms + 7) == at.after);
        CHECK(Scheduler::state_at(at.from_ms + 10) == at.after);
    }
    CHECK(Scheduler::state_at(0) == SlotState::Slot1);
    CHECK(Scheduler::state_at(999) == SlotState::Slot1);
}

// 200-202 B, 202-207 retune, 207-442 O, 442-447 retune, 447-822 A, 822-827 retune, 827-1202 B.
TEST_CASE("timing: a dwell listens from its lead to its margin, and a retune plans the next one") {
    CHECK(Scheduler::plan(300, anchored()).start_ms == 207);
    CHECK(Scheduler::plan(300, anchored()).end_ms == 442);
    CHECK(Scheduler::plan(500, anchored()).start_ms == 447);
    CHECK(Scheduler::plan(500, anchored()).end_ms == 822);
    CHECK(Scheduler::plan(900, anchored()).start_ms == 827);
    CHECK(Scheduler::plan(900, anchored()).end_ms == 1202);
    // The tail is the same dwell as 900 ms.
    CHECK(Scheduler::plan(100, anchored()).start_ms == 827);
    CHECK(Scheduler::plan(201, anchored()).end_ms == 1202);
    CHECK(kWindowBSpillMs == 202);
    CHECK(Scheduler::plan(204, anchored()).start_ms == 207);
    CHECK(Scheduler::plan(444, anchored()).start_ms == 447);
    CHECK(Scheduler::plan(824, anchored()).start_ms == 827);
}

TEST_CASE("timing: band and channel per dwell") {
    CHECK(Scheduler::plan(300, anchored()).band == Band::O);
    CHECK(Scheduler::plan(300, anchored()).frequency_hz == adsl::kOBandHz);
    // The tail of slot 1: still M-band, still the second channel.
    CHECK(Scheduler::plan(100, anchored()).frequency_hz == adsl::kMBand1Hz);
    CHECK(Scheduler::slot_of(100) == 1);
    // §C.2: two M-band channels, one per direct half, so a receiver visits both.
    CHECK(Scheduler::plan(500, anchored()).frequency_hz == adsl::kMBand0Hz);
    CHECK(Scheduler::plan(900, anchored()).frequency_hz == adsl::kMBand1Hz);
    CHECK(Scheduler::slot_of(500) == 0);
    CHECK(Scheduler::slot_of(900) == 1);
    CHECK(Scheduler::slot_of(300) == -1);
}

TEST_CASE("timing: the direct slot is narrower than the M-band dwells") {
    CHECK_FALSE(Scheduler::in_direct_slot(447));
    CHECK(Scheduler::in_direct_slot(450));
    CHECK(Scheduler::in_direct_slot(999));
    CHECK_FALSE(Scheduler::in_direct_slot(100));
    CHECK_FALSE(Scheduler::in_direct_slot(300));
}

// The executor cannot be handed a burst mid-dwell: a plan queues behind the one it is flying.
TEST_CASE("timing: the dwell owns its burst from the instant it opens") {
    CHECK(Scheduler::plan(444, anchored()).tx_allowed);
    CHECK(Scheduler::plan(447, anchored()).tx_allowed);
    CHECK(Scheduler::plan(824, anchored()).tx_allowed);
    // Slot 1's tail is the same dwell, and it carries the callsign burst.
    CHECK(Scheduler::plan(0, anchored()).tx_allowed);
    CHECK(Scheduler::plan(kWindowBSpillMs - 1, anchored()).tx_allowed);
    // The uplink dwell and the retune into it own no direct-slot time at all.
    CHECK_FALSE(Scheduler::plan(204, anchored()).tx_allowed);
    CHECK_FALSE(Scheduler::plan(300, anchored()).tx_allowed);
}

TEST_CASE("timing: TX only in M-band direct slots when clock is anchored") {
    Scheduler scheduler;
    SlotPlan plan = scheduler.plan(500, anchored());
    CHECK(plan.tx_allowed);
    CHECK(plan.band == Band::M);
    CHECK_FALSE(plan.listen_only);
    // The uplink window belongs to the ground infrastructure.
    plan = scheduler.plan(300, anchored());
    CHECK_FALSE(plan.tx_allowed);
    CHECK(plan.band == Band::O);
}

// C.5 and G.1.1: the held edge keeps the second, so the direct slot stays the direct slot.
TEST_CASE("timing: PPS lost within holdover keeps the dwells and transmits on the held edge") {
    Scheduler scheduler;
    ClockState clock{true, false, 5000, kLastEdgeUs};
    SlotPlan plan = scheduler.plan(500, clock);
    CHECK_FALSE(plan.listen_only);
    CHECK(plan.tx_allowed);
    CHECK(plan.drift_ms == 1);
    CHECK(scheduler.plan(500, anchored()).drift_ms == 0);
}

// The bench's 152 ppm over a whole minute held is 9.1 ms, inside G.1.1's 10 ms.
TEST_CASE("timing: a held second drifts by the bench's 152 ppm, at most 10 ms") {
    CHECK(drift_ms_after(0) == 0);
    CHECK(drift_ms_after(1) == 1);
    CHECK(drift_ms_after(kPpsHoldoverMs) == 10);
    CHECK(holdover_drift_ms(ClockState{true, false, kPpsHoldoverMs, kLastEdgeUs}) == 10);
    CHECK(holdover_drift_ms(ClockState{true, false, kPpsHoldoverMs + 1, kLastEdgeUs}) == 0);
    CHECK(holdover_drift_ms(ClockState{true, true, 900, kLastEdgeUs}) == 0);
    CHECK(pps_anchored(ClockState{true, false, kPpsHoldoverMs, kLastEdgeUs}));
    CHECK_FALSE(pps_anchored(ClockState{true, false, kPpsHoldoverMs + 1, kLastEdgeUs}));
}

// A burst drained between the edge and the sentence naming it was dated a second early.
TEST_CASE("timing: the dated second moves with the edge, not with the sentence") {
    ClockState clock{};
    clock.utc_valid = true;
    clock.pps_locked = true;
    clock.utc_s = 45296;
    clock.utc_edge_us = 12'000'000;
    clock.pps_edge_us = 12'000'000;

    carry_utc_to_edge(clock, 12'000'000);
    CHECK(clock.utc_s == 45296);

    carry_utc_to_edge(clock, 13'000'000);
    CHECK(clock.utc_s == 45297);
    CHECK(clock.pps_edge_us == 13'000'000);

    // A pass that saw no edge for three seconds catches up on the one it does see.
    carry_utc_to_edge(clock, 16'000'100);
    CHECK(clock.utc_s == 45300);
    CHECK(clock.utc_edge_us == 16'000'100);
}

TEST_CASE("timing: a clock no solution has dated yet carries no second to advance") {
    ClockState clock{};
    carry_utc_to_edge(clock, 13'000'000);
    CHECK(clock.utc_s == 0);
    CHECK(clock.pps_edge_us == 13'000'000);
}

TEST_CASE("timing: past holdover or no UTC, listen only, fail closed") {
    Scheduler scheduler;
    ClockState past{true, false, kPpsHoldoverMs + 1};
    CHECK(scheduler.plan(500, past).listen_only);
    CHECK_FALSE(scheduler.plan(500, past).tx_allowed);

    ClockState no_utc{false, true, 0};
    CHECK(scheduler.plan(500, no_utc).listen_only);
    CHECK_FALSE(scheduler.plan(500, no_utc).tx_allowed);
}

namespace {

bool same_dwell(const SlotPlan& first, const SlotPlan& second) {
    return first.state == second.state && first.band == second.band &&
           first.frequency_hz == second.frequency_hz && first.start_ms == second.start_ms &&
           first.end_ms == second.end_ms && first.tx_allowed == second.tx_allowed;
}

}  // namespace

// Pps::since_edge_ms() reads 0 before the first edge, which used to pass for an edge just now.
TEST_CASE("timing: an edge never seen is no holdover, and plans the same dwells as before") {
    const ClockState never{true, false, 0, 0};
    const ClockState past{true, false, kPpsHoldoverMs + 1, kLastEdgeUs};
    const ClockState held{true, false, 5000, kLastEdgeUs};
    CHECK_FALSE(in_pps_holdover(never));
    CHECK(in_pps_holdover(held));
    for (int phase = 0; phase < 1000; phase++) {
        const SlotPlan plan = Scheduler::plan(phase, never);
        CHECK(same_dwell(plan, Scheduler::plan(phase, past)));
        CHECK(plan.start_ms == Scheduler::plan(phase, held).start_ms);
        CHECK(plan.end_ms == Scheduler::plan(phase, held).end_ms);
        CHECK_FALSE(plan.tx_allowed);
        CHECK(plan.listen_only == Scheduler::plan(phase, past).listen_only);
    }
}

// Bench, 28sep26: an edge latched between the two reads made the edge 584,000 years old for one
// pass, so the clock called PPS lost and the radio re-planned against a free-running second.
TEST_CASE("timing: an edge latched after now was read is no age at all, not an underflow") {
    CHECK(since_edge_us(27000000, 27798000) == 798000);
    CHECK(since_edge_us(28000000, 27999990) == 0);
    CHECK(since_edge_us(28000000, 28000000) == 0);
}
