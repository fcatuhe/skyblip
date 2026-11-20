// What the pilot reads when the Go stops transmitting: the mark on every page, the self-test rows.
#include <cstdint>

#include "core/timing/slot.h"
#include "core/timing/transmit_gate.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/no_tx.h"
#include "products/skyblip_go/pages/self_test.h"
#include "products/skyblip_go/self_test_parts.h"
#include "simulator/simulator.h"
#include "test/support/glass_text.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

bool wears_no_tx(simulator::Simulator& simulator, const char* reason) {
    return reads_at(simulator.panel(), go::kNoTxMargin, go::kNoTxMargin, go::kNoTxWord, 1, false) &&
           reads_at(simulator.panel(), go::kNoTxMargin, go::kNoTxReasonY, reason, 1, false);
}

// INFO: fc 19nov26 the world's own tap and press, and the passes that read them
uint32_t tap(simulator::Simulator& simulator, uint32_t now_ms) {
    simulator.world().tap_pad();
    run_on(simulator, now_ms, 800);
    return now_ms + 805;
}

uint32_t press(simulator::Simulator& simulator, uint32_t now_ms) {
    simulator.world().press_button();
    run_on(simulator, now_ms, 800);
    return now_ms + 805;
}

// INFO: fc 19nov26 from the radar: tap to nearby, open its menu, walk to the page's row, open it
uint32_t open_from_nearby(simulator::Simulator& simulator, uint32_t now_ms, go::MenuRow page) {
    now_ms = tap(simulator, now_ms);
    now_ms = press(simulator, now_ms);
    for (int row = 0; row < 8 && simulator.product().screen().editor().focus() != page; row++)
        now_ms = tap(simulator, now_ms);
    now_ms = press(simulator, now_ms);
    run_on(simulator, now_ms, 2000);
    return now_ms + 2005;
}

int row_of(ports::Capability capability) {
    for (int i = 0; i < go::kSelfTestPartCount; i++)
        if (go::kSelfTestParts[i].capability == capability) return i;
    return -1;
}

bool row_reads(simulator::Simulator& simulator, ports::Capability capability, const char* text) {
    const int y = go::self_test_row_y(row_of(capability));
    return reads_in(simulator.panel(), text, 0, y, go::kGlassWidth, y + go::kSelfTestRowHeight);
}

}  // namespace

TEST_CASE("no tx glass: a PPS lost past holdover puts NO TX PPS on the radar, then takes it off") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    const uint32_t settled_ms = past_settling(simulator);
    run_on(simulator, settled_ms, 3000);
    CHECK_FALSE(wears_no_tx(simulator, "PPS"));

    simulator.world().set_pps_locked(false);
    const uint32_t lost_ms = settled_ms + 3005;
    run_on(simulator, lost_ms, timing::kPpsHoldoverMs + timing::TransmitGate::kNoTxHoldMs + 3000);
    REQUIRE(simulator.product().screen().page() == go::Page::Radar);
    CHECK(wears_no_tx(simulator, "PPS"));

    simulator.world().set_pps_locked(true);
    run_on(simulator, lost_ms + timing::kPpsHoldoverMs + timing::TransmitGate::kNoTxHoldMs + 3005,
           3000);
    CHECK_FALSE(wears_no_tx(simulator, "PPS"));
}

TEST_CASE("no tx glass: the self test says how the radio and the receiver work now") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    const uint32_t settled_ms = past_settling(simulator);
    uint32_t now_ms = open_from_nearby(simulator, settled_ms, go::MenuRow::SelfTest);
    REQUIRE(simulator.product().screen().page() == go::Page::SelfTest);
    CHECK(row_reads(simulator, ports::Capability::Rf, "SX1262 CCA OK"));
    CHECK(row_reads(simulator, ports::Capability::Gnss, "L76K 2 HZ"));
    CHECK(row_reads(simulator, ports::Capability::Gnss, "PASS"));

    simulator.world().set_pps_locked(false);
    run_on(simulator, now_ms, timing::kPpsHoldoverMs + timing::TransmitGate::kNoTxHoldMs + 2000);
    REQUIRE(simulator.product().screen().page() == go::Page::SelfTest);
    CHECK(row_reads(simulator, ports::Capability::Gnss, "L76K NO PPS"));
    CHECK(row_reads(simulator, ports::Capability::Gnss, go::kSelfTestNoTxVerdict));
    CHECK(row_reads(simulator, ports::Capability::Rf, "PASS"));
    CHECK(wears_no_tx(simulator, "PPS"));
}

TEST_CASE("no tx glass: in flight the Go shakes once as NO TX appears, not again while it stays") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(120);
    const uint32_t settled_ms = past_settling(simulator);
    run_on(simulator, settled_ms, 20000);
    REQUIRE(simulator.product().state().flight.confirmed_state == flight::FlightState::Airborne);

    simulator.world().set_pps_locked(false);
    const uint32_t lost_ms = settled_ms + 20005;
    const uint32_t shown_ms = lost_ms + timing::kPpsHoldoverMs + timing::TransmitGate::kNoTxHoldMs;
    run_on(simulator, lost_ms, shown_ms - lost_ms - 2000);
    const uint32_t before = simulator.platform().annunciator().motor_starts();
    const uint32_t cuts = simulator.platform().annunciator().guard_cuts();
    run_on(simulator, shown_ms - 1995, 4000);
    REQUIRE(wears_no_tx(simulator, "PPS"));
    CHECK(simulator.platform().annunciator().motor_starts() == before + 1);
    run_on(simulator, shown_ms + 2010, 30000);
    CHECK(simulator.platform().annunciator().motor_starts() == before + 1);
    CHECK_FALSE(simulator.platform().annunciator().vibrating());
    CHECK(simulator.platform().annunciator().guard_cuts() == cuts);
}

TEST_CASE("no tx glass: on the ground NO TX appears without a shake") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(0);
    const uint32_t settled_ms = past_settling(simulator);
    REQUIRE(simulator.product().state().flight.confirmed_state != flight::FlightState::Airborne);
    const uint32_t before = simulator.platform().annunciator().motor_starts();
    simulator.world().set_pps_locked(false);
    run_on(simulator, settled_ms,
           timing::kPpsHoldoverMs + timing::TransmitGate::kNoTxHoldMs + 3000);
    REQUIRE(wears_no_tx(simulator, "PPS"));
    CHECK(simulator.platform().annunciator().motor_starts() == before);
}

TEST_CASE("no tx glass: a radio that stops answering reads NO CCA and NO TX on the self test") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    const uint32_t settled_ms = past_settling(simulator);
    const uint32_t now_ms = open_from_nearby(simulator, settled_ms, go::MenuRow::SelfTest);
    REQUIRE(simulator.product().screen().page() == go::Page::SelfTest);
    simulator.platform().chips().radio.busy_stuck = true;
    run_on(simulator, now_ms,
           go::RadioService::kRadioAnswerMaxAgeMs + timing::TransmitGate::kNoTxHoldMs + 2000);
    CHECK(row_reads(simulator, ports::Capability::Rf, "SX1262 NO CCA"));
    CHECK(row_reads(simulator, ports::Capability::Rf, go::kSelfTestNoTxVerdict));
    CHECK(row_reads(simulator, ports::Capability::Gnss, "PASS"));
    CHECK(wears_no_tx(simulator, "RADIO"));
}

TEST_CASE("no tx glass: the status page names what the Go waits on beside TX OFF") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    const uint32_t settled_ms = past_settling(simulator);
    const uint32_t now_ms = open_from_nearby(simulator, settled_ms, go::MenuRow::Status);
    REQUIRE(simulator.product().screen().page() == go::Page::Status);
    CHECK(reads_in(simulator.panel(), "TX ON", 100, 0, go::kGlassWidth, go::kGlassHeight));
    simulator.world().set_pps_locked(false);
    run_on(simulator, now_ms, timing::kPpsHoldoverMs + 2000);
    CHECK(reads_in(simulator.panel(), "TX OFF PPS", 100, 0, go::kGlassWidth, go::kGlassHeight));
}
