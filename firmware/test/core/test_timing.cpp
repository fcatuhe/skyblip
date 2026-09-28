// One radio, one second, two bands: the dwell map decides what the receiver can
// hear and when it is allowed to speak. These pin down both halves. A dwell that
// runs past its edge or a burst that starts too late to finish inside the direct
// slot transmits into someone else's window, and a device that keeps transmitting
// once UTC is gone does it blind. Without a clock the answer is listen only.
#include "core/timing/slot.h"
#include "doctest/doctest.h"
#include "test/support/anchored_clock.h"

using namespace skyblip::timing;

constexpr uint64_t kLastEdgeUs = 12'000'000;

// Our cut of the second: two 400 ms M-band dwells, one channel each, so slot 1
// runs 800..1200 and its tail is the head of the next second. The uplink dwell
// is framed on the window our own ground station transmits in (210..390 ms, a
// burst that completes by 390). The M-band dwell opens at 400 to catch
// FLARM-generation traffic that starts around 405, earlier than the ADS-L direct
// slot our own bursts respect.
TEST_CASE("timing: dwell map matches the decided band split") {
    CHECK(Scheduler::state_at(0) == SlotState::Slot1);
    CHECK(Scheduler::state_at(199) == SlotState::Slot1);
    CHECK(Scheduler::state_at(200) == SlotState::SwitchMtoO);
    CHECK(Scheduler::state_at(204) == SlotState::SwitchMtoO);
    CHECK(Scheduler::state_at(205) == SlotState::UplinkRxO);
    CHECK(Scheduler::state_at(390) == SlotState::UplinkRxO);
    CHECK(Scheduler::state_at(394) == SlotState::UplinkRxO);
    CHECK(Scheduler::state_at(395) == SlotState::SwitchOtoM);
    CHECK(Scheduler::state_at(399) == SlotState::SwitchOtoM);
    CHECK(Scheduler::state_at(400) == SlotState::Slot0);
    CHECK(Scheduler::state_at(796) == SlotState::Slot0);
    CHECK(Scheduler::state_at(797) == SlotState::Hop);
    CHECK(Scheduler::state_at(799) == SlotState::Hop);
    CHECK(Scheduler::state_at(800) == SlotState::Slot1);
    CHECK(Scheduler::state_at(999) == SlotState::Slot1);
}

TEST_CASE("timing: every band edge keeps its guard") {
    CHECK(kUplinkRxStart <= kGroundEmitStart - kJitterGuardMs);
    CHECK(kUplinkRxEnd >= kGroundEmitEnd + kJitterGuardMs);
    // M->O after slot 1's tail, then O->M before slot 0.
    CHECK(kUplinkRxStart - kSlot1Wrap == kJitterGuardMs);
    CHECK(kSlot0Start - kUplinkRxEnd == kJitterGuardMs);
    // Two equal M-band dwells, one channel each.
    CHECK(kSlot0End - kSlot0Start == kSlot1End - kSlot1Start);
}

TEST_CASE("timing: band and channel per dwell") {
    CHECK(Scheduler::band_at(300) == Band::O);
    // The tail of slot 1: still M-band, still the second channel.
    CHECK(Scheduler::band_at(100) == Band::M);
    CHECK(Scheduler::freq_at(100) == kMband1Hz);
    CHECK(Scheduler::slot_of(100) == 1);
    CHECK(Scheduler::band_at(420) == Band::M);
    CHECK(Scheduler::band_at(500) == Band::M);
    CHECK(Scheduler::band_at(900) == Band::M);
    // §C.2: two M-band channels, one per direct half, so a receiver visits both.
    CHECK(Scheduler::freq_at(300) == kObandHz);
    CHECK(Scheduler::freq_at(420) == kMband0Hz);
    CHECK(Scheduler::freq_at(500) == kMband0Hz);
    CHECK(Scheduler::freq_at(900) == kMband1Hz);
}

TEST_CASE("timing: uplink window and direct slot predicates") {
    CHECK(Scheduler::in_uplink_rx(205));
    CHECK(Scheduler::in_uplink_rx(394));
    CHECK_FALSE(Scheduler::in_uplink_rx(395));
    // The dwells are wider than the direct slot at both ends: we listen from
    // 400 and to 1200, we transmit only inside 450..1000.
    CHECK_FALSE(Scheduler::in_direct_slot(420));
    CHECK(Scheduler::in_direct_slot(450));
    CHECK(Scheduler::in_direct_slot(999));
    CHECK_FALSE(Scheduler::in_direct_slot(100));
    CHECK_FALSE(Scheduler::in_direct_slot(300));
    CHECK(Scheduler::slot_of(420) == 0);
    CHECK(Scheduler::slot_of(500) == 0);
    CHECK(Scheduler::slot_of(900) == 1);
    CHECK(Scheduler::slot_of(300) == -1);
}

// The executor cannot be handed a burst mid-dwell: a plan queues behind the one it is flying.
TEST_CASE("timing: the dwell owns its burst from the instant it opens") {
    Scheduler s;
    // Slot 0 opens at 400 and places its burst from 450, so 400 is where the plan must carry it.
    CHECK_FALSE(Scheduler::in_direct_slot(kSlot0Start));
    CHECK(Scheduler::in_own_tx_dwell(kSlot0Start));
    CHECK(s.plan(kSlot0Start, anchored()).tx_allowed);
    CHECK(Scheduler::in_own_tx_dwell(kSlot1Start));
    CHECK(s.plan(kSlot1Start, anchored()).tx_allowed);
    // Slot 1's tail is the same dwell, and it carries the callsign burst.
    CHECK(Scheduler::in_own_tx_dwell(0));
    CHECK(Scheduler::in_own_tx_dwell(kSlot1Wrap - 1));
    CHECK(s.plan(100, anchored()).tx_allowed);
    // The uplink dwell and both band edges own no direct-slot time at all.
    CHECK_FALSE(Scheduler::in_own_tx_dwell(202));
    CHECK_FALSE(Scheduler::in_own_tx_dwell(300));
    CHECK_FALSE(Scheduler::in_own_tx_dwell(397));
}

TEST_CASE("timing: a dwell stops early enough to retune before the next one") {
    Scheduler s;
    // The O->M edge is the safety-critical one: 5 ms of guard, then M-band at 400.
    CHECK(s.plan(300, anchored()).start_ms == kUplinkRxStart);
    CHECK(s.plan(300, anchored()).end_ms == kUplinkRxEnd);
    // The tail is the same dwell as 900 ms: one plan, 800..1200.
    CHECK(s.plan(100, anchored()).start_ms == kSlot1Start);
    CHECK(s.plan(100, anchored()).end_ms == kSlot1End);
    CHECK(s.plan(500, anchored()).start_ms == kSlot0Start);
    CHECK(s.plan(500, anchored()).end_ms == kSlot0End - kHopGuardMs);
    CHECK(s.plan(900, anchored()).end_ms == kSlot1End);
    CHECK(s.plan(900, anchored()).start_ms == kSlot1Start);
}

TEST_CASE("timing: TX only in M-band direct slots when clock is anchored") {
    Scheduler s;
    SlotPlan p = s.plan(500, anchored());
    CHECK(p.tx_allowed);
    CHECK(p.band == Band::M);
    CHECK_FALSE(p.listen_only);
    // The uplink window belongs to the ground infrastructure.
    p = s.plan(300, anchored());
    CHECK_FALSE(p.tx_allowed);
    CHECK(p.band == Band::O);
}

TEST_CASE("timing: PPS lost within holdover, still receiving, no slotted TX") {
    Scheduler s;
    ClockState c{true, false, 5000, kLastEdgeUs};
    SlotPlan p = s.plan(500, c);
    CHECK_FALSE(p.listen_only);
    CHECK_FALSE(p.tx_allowed);
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
    Scheduler s;
    ClockState past{true, false, kPpsHoldoverMs + 1};
    CHECK(s.plan(500, past).listen_only);
    CHECK_FALSE(s.plan(500, past).tx_allowed);

    ClockState no_utc{false, true, 0};
    CHECK(s.plan(500, no_utc).listen_only);
    CHECK_FALSE(s.plan(500, no_utc).tx_allowed);
}

namespace {

bool same_dwell(const SlotPlan& a, const SlotPlan& b) {
    return a.state == b.state && a.band == b.band && a.freq_hz == b.freq_hz &&
           a.start_ms == b.start_ms && a.end_ms == b.end_ms && a.tx_allowed == b.tx_allowed &&
           a.own_tx_dwell == b.own_tx_dwell;
}

}  // namespace

// Pps::ms_since() reads 0 before the first edge, which used to pass for an edge just now.
TEST_CASE("timing: an edge never seen is no holdover, and plans the same dwells as before") {
    const ClockState never{true, false, 0, 0};
    const ClockState past{true, false, kPpsHoldoverMs + 1, kLastEdgeUs};
    const ClockState held{true, false, 5000, kLastEdgeUs};
    CHECK_FALSE(in_pps_holdover(never));
    CHECK(in_pps_holdover(held));
    for (int phase = 0; phase < 1000; phase++) {
        const SlotPlan p = Scheduler::plan(phase, never);
        CHECK(same_dwell(p, Scheduler::plan(phase, past)));
        CHECK(same_dwell(p, Scheduler::plan(phase, held)));
        CHECK_FALSE(p.tx_allowed);
        CHECK(p.listen_only == Scheduler::plan(phase, past).listen_only);
    }
}

// Bench, 28sep26: an edge latched between the two reads made the edge 584,000 years old for one
// pass, so the clock called PPS lost and the radio re-planned against a free-running second.
TEST_CASE("timing: an edge latched after now was read is no age at all, not an underflow") {
    CHECK(since_edge_us(27000000, 27798000) == 798000);
    CHECK(since_edge_us(28000000, 27999990) == 0);
    CHECK(since_edge_us(28000000, 28000000) == 0);
}
