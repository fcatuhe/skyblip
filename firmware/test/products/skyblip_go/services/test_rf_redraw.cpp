// At 2 Hz a position drawn before its .500 fix came is drawn again, and counted apart from a loss.
#include <cstdint>

#include "core/settings/address.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "test/support/anchored_clock.h"
#include "test/support/armings_rig.h"
#include "test/support/rf_pass_rig.h"

using namespace skyblip;

namespace {

constexpr uint32_t kUtcS = 1000;
constexpr int kWholeInHandMs = 70;

// A serial whose position on A in kUtcS is drawn between first and last.
uint16_t serial_drawn_between(int first_ms, int last_ms) {
    timing::Scheduler scheduler;
    const timing::SlotPlan window_a = scheduler.plan(500, timing::anchored());
    for (uint16_t serial = 0x100; serial < 0x7F00; serial++) {
        timing::Transmitter transmitter;
        transmitter.configure(settings::address_of(serial));
        const timing::Transmitter::Attempt drawn =
            transmitter.attempt(window_a, kUtcS, kUtcS * 1000, true, 0,
                                timing::Transmitter::Payload::Position, kWholeInHandMs);
        if ((drawn.go || drawn.stale) && drawn.at_ms >= first_ms && drawn.at_ms <= last_ms)
            return serial;
    }
    FAIL("no serial draws an instant in that range");
    return 0;
}

// One second of a 2 Hz receiver: the .000 solution in hand at 70 ms, the .500 one at half_in_ms.
// With busy_once, the first burst armed finds its channel busy at its instant.
void fly_second(Armings& armings, uint32_t second, int half_in_ms, bool busy_once = false) {
    const uint32_t edge_ms = static_cast<uint32_t>(Armings::kEdgeUs / 1000) + second * 1000;
    armings.state.own.utc_s = kUtcS + second;
    for (int phase = timing::kWindowBSpillMs + 10; phase <= 990; phase += 10) {
        const bool half = phase >= half_in_ms;
        armings.state.own.utc_ms = half ? 500 : 0;
        armings.state.own.fix_at_ms = edge_ms + (half ? 500 : 0);
        armings.state.own.fix_in_hand_ms = edge_ms + static_cast<uint32_t>(half ? half_in_ms : 70);
        armings.tick_in(second, phase);
        const uint64_t now_us = Armings::kEdgeUs + second * 1000000ull + phase * 1000ull;
        for (uint32_t i = 0; busy_once && i < armings.rf.handed; i++) {
            const ports::RfPlan& plan = armings.rf.plans[i];
            if (plan.tx == nullptr || plan.tx_at_us > now_us) continue;
            armings.state.rf.note_busy({plan.tx_at_us, -50, true});
            busy_once = false;
        }
    }
}

uint64_t position_tx_at_us(const Armings& armings) {
    for (uint32_t i = 0; i < armings.rf.handed; i++)
        if (armings.rf.plans[i].tx != nullptr) return armings.rf.plans[i].tx_at_us;
    return 0;
}

}  // namespace

// The pinned case: drawn at .520, its .500 fix in hand at .590, and nothing went on air.
TEST_CASE("rf: a position whose .500 fix came after its instant is redrawn and goes out") {
    Armings armings;
    armings.state.activation.serial = serial_drawn_between(510, 530);
    armings.ready_to_transmit(kUtcS);
    armings.solving = false;
    REQUIRE(armings.radio.setup() == Status::Ok);

    fly_second(armings, 0, 590);
    armings.state.own.utc_s = kUtcS + 1;
    armings.tick_in(1, 300);

    const uint64_t tx_at_us = position_tx_at_us(armings);
    REQUIRE(tx_at_us != 0);
    const int64_t at_ms = static_cast<int64_t>((tx_at_us - Armings::kEdgeUs) / 1000);
    CHECK(at_ms >= 590 + timing::Transmitter::kRedrawLeadMs);
    CHECK(at_ms <= timing::Transmitter::last_instant(timing::Transmitter::Payload::Position, 0));
    CHECK(at_ms - 500 <=
          timing::Transmitter::kFixMaxAgeMs - timing::Transmitter::kFixAgeAllowanceMs);
    CHECK(armings.state.air.tx_redrawn_for_fix == 1);
    CHECK(armings.state.air.tx_held_for_fix == 0);
}

TEST_CASE("rf: a position whose .500 fix comes after .810 is lost, and counted as lost once") {
    Armings armings;
    armings.state.activation.serial = serial_drawn_between(510, 530);
    armings.ready_to_transmit(kUtcS);
    armings.solving = false;
    REQUIRE(armings.radio.setup() == Status::Ok);

    fly_second(armings, 0, 820);
    armings.state.own.utc_s = kUtcS + 1;
    armings.tick_in(1, 300);

    CHECK(position_tx_at_us(armings) == 0);
    CHECK(armings.state.air.tx_redrawn_for_fix == 0);
    CHECK(armings.state.air.tx_held_for_fix == 1);
}

// The pinned case: serial 1 drawn on A past .490 waits for its .500 fix, in hand at .808.
TEST_CASE("rf: a position redrawn to .811, past A's last instant, keys and ends inside its slot") {
    Pass pass;
    REQUIRE(pass.begin() == Status::Ok);
    pass.state.activation.serial = 1;
    pass.state.own.fix_at_ms = 2000;
    pass.state.own.fix_in_hand_ms = 2070;
    pass.pass_on_fix(2420000);
    pass.pass_on_fix(2450000);
    pass.pass_on_fix(2600000);
    REQUIRE_FALSE(pass.state.rf.dwell.burst_armed);

    pass.state.own.fix_at_ms = 2500;
    pass.state.own.fix_in_hand_ms = 2808;
    pass.pass_on_fix(2809000);
    pass.executor_until(2812000, 1);
    pass.chip.signal_tx_done();
    pass.pass_on_fix(2817000);

    CHECK(pass.first_tx_us == 2811000);
    // C.5's slot and G.1.16's age of the .500 fix both end at 3000 ms, C.2's frame is 4640 us.
    CHECK(pass.first_tx_us + 4640 <= 3000000);
    CHECK(pass.state.air.tx_redrawn_for_fix == 1);
    CHECK(pass.state.air.tx_ok == 1);
    CHECK(pass.state.air.tx_lost == 0);
}

// The pinned case: drawn at .48x, deferred past 490 ms on a busy channel, and missed for its age.
TEST_CASE("rf: a deferred position too old for its .000 fix waits for the .500 one, redrawn") {
    Armings armings;
    armings.state.activation.serial = serial_drawn_between(478, 486);
    armings.ready_to_transmit(kUtcS);
    armings.solving = false;
    REQUIRE(armings.radio.setup() == Status::Ok);

    fly_second(armings, 0, 590, /*busy_once=*/true);
    armings.state.own.utc_s = kUtcS + 1;
    armings.tick_in(1, 300);

    REQUIRE(armings.state.air.tx_deferred == 1);
    uint64_t redrawn_us = 0;
    for (uint32_t i = 0; i < armings.rf.handed; i++)
        if (armings.rf.plans[i].tx != nullptr) redrawn_us = armings.rf.plans[i].tx_at_us;
    const int64_t at_ms = static_cast<int64_t>((redrawn_us - Armings::kEdgeUs) / 1000);
    CHECK(at_ms >= 590 + timing::Transmitter::kRedrawLeadMs);
    CHECK(armings.state.air.tx_redrawn_for_fix == 1);
    CHECK(armings.state.air.tx_busy_missed == 0);
    CHECK(armings.state.air.tx_held_for_fix == 0);
}
