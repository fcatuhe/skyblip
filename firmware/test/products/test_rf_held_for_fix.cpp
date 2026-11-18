// A position held for its fix, as the per-dwell record and the radio counters report it.
#include <cstdint>

#include "core/diagnostics/payload.h"
#include "doctest/doctest.h"
#include "test/support/armings_rig.h"

using namespace skyblip;

// The pinned case: a position held for a stale fix was recorded as a second off schedule.
TEST_CASE("rf: a dwell that held a position for its fix records why, not off schedule") {
    Armings armings;
    armings.recorder.arm();
    armings.ready_to_transmit(1000);
    armings.solving = false;
    armings.state.own.fix_at_ms = static_cast<uint32_t>(Armings::kEdgeUs / 1000) - 1000;
    REQUIRE(armings.radio.setup() == Status::Ok);
    for (int phase = 200; phase <= 990; phase += 10) armings.tick_at(phase);

    diagnostics::Record records[diagnostics::Recorder::kMaxRecords];
    const int count = armings.recorder.peek(records, diagnostics::Recorder::kMaxRecords);
    int held = 0;
    for (int i = 0; i < count; i++) {
        diagnostics::Dwell dwell{};
        if (diagnostics::read(records[i], dwell) &&
            dwell.refusal == diagnostics::Refusal::HeldForFix)
            held++;
    }
    CHECK(held > 0);
    CHECK(armings.state.air.tx_held_for_fix > 0);
}

// A fix no timely fix anchored has no age to judge, so its position is held however fresh it looks.
TEST_CASE("rf: a position whose fix no timely fix anchored is held and counted, never armed") {
    Armings armings;
    armings.ready_to_transmit(1000);
    armings.state.own.fix_dated = false;
    REQUIRE(armings.radio.setup() == Status::Ok);
    for (int phase = 200; phase <= 990; phase += 10) armings.tick_at(phase);

    CHECK(armings.rf.with_tx == 0);
    CHECK(armings.state.air.tx_held_for_fix > 0);

    armings.state.own.fix_dated = true;
    for (int phase = 200; phase <= 990; phase += 10) armings.tick_in(1, phase);
    CHECK(armings.rf.with_tx > 0);
}
