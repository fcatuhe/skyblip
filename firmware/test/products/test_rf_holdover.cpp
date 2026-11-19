// ADS-L 4 SRD-860 issue 2 C.5 and G.1.1 through a lost PPS: the Go keeps its second for the
// holdover.
#include <cstdint>

#include "core/timing/slot.h"
#include "doctest/doctest.h"
#include "protocols/adsl/transmit.h"
#include "simulator/simulator.h"
#include "test/support/fresh_fix.h"
#include "test/support/rf_pass_rig.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

int positions_between(const simulator::Air& air, uint64_t from_us, uint64_t to_us) {
    int count = 0;
    for (const simulator::AirRecord& record : records_of(air, adsl::Packet::kPayloadTypePosition))
        if (record.at_us >= from_us && record.at_us < to_us) count++;
    return count;
}

}  // namespace

TEST_CASE("rf holdover: a Go that loses its PPS keeps transmitting inside the direct slot") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(120);
    const uint32_t settled_ms = past_settling(simulator);
    run_on(simulator, settled_ms, 5000);

    const uint32_t lost_ms = settled_ms + 5010;
    simulator.world().set_pps_locked(false);
    run_on(simulator, lost_ms, 30000);

    const uint64_t lost_us = uint64_t{lost_ms} * 1000;
    CHECK(positions_between(simulator.world().air(), lost_us, lost_us + 30'000'000) >= 25);
    for (const simulator::AirRecord& record :
         records_of(simulator.world().air(), adsl::Packet::kPayloadTypePosition)) {
        const uint64_t phase_us = record.at_us % 1'000'000;
        CHECK(phase_us >= uint64_t{adsl::kDirectStartMs} * 1000);
        CHECK(phase_us + adsl::kAirTimeMs * 1000 <= uint64_t{adsl::kDirectEndMs} * 1000);
    }
}

TEST_CASE("rf holdover: past the holdover the Go stops transmitting, and resumes on the edge") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(120);
    const uint32_t settled_ms = past_settling(simulator);

    simulator.world().set_pps_locked(false);
    const uint32_t past_ms = settled_ms + timing::kPpsHoldoverMs + 2000;
    run_on(simulator, settled_ms, timing::kPpsHoldoverMs + 2000);
    simulator.world().air().clear();
    run_on(simulator, past_ms, 10000);
    CHECK(count_payloads(simulator.world().air(), adsl::Packet::kPayloadTypePosition) == 0);

    simulator.world().set_pps_locked(true);
    run_on(simulator, past_ms + 10010, 10000);
    CHECK(count_payloads(simulator.world().air(), adsl::Packet::kPayloadTypePosition) >= 5);
}

// The held edge is not on the uptime's second, and the burst keys on the edge's.
TEST_CASE("rf holdover: a burst on a held second keys at its instant from the held edge") {
    Pass pass;
    REQUIRE(pass.begin() == Status::Ok);
    const uint64_t held_us = 2'300'000;
    const uint64_t at_service_us = held_us + 10'460'000;
    pass.platform.clock().set_microseconds(at_service_us);
    pass.state.clock.pps_locked = false;
    pass.state.clock.pps_edge_us = held_us;
    pass.state.clock.since_edge_ms = 10460;
    pass.state.clock.utc_s = 2;
    pass.state.clock.utc_edge_us = held_us;
    REQUIRE(timing::in_pps_holdover(pass.state.clock));
    pass.services_at(at_service_us);
    CHECK(pass.state.rf.dwell.phase_ms == 460);

    const uint32_t now_ms = static_cast<uint32_t>(at_service_us / 1000);
    const timing::SlotPlan plan = timing::Scheduler::plan(500, pass.state.clock);
    const timing::Transmitter::Attempt wanted =
        pass.radio_service.transmitter().attempt(plan, 12, now_ms, true, kFixSolvedAtSecondEnd);
    REQUIRE(wanted.go);
    pass.executor_until(held_us + 10'800'000);
    REQUIRE(pass.first_tx_us != 0);
    const int64_t error_us = static_cast<int64_t>(pass.first_tx_us) -
                             static_cast<int64_t>(held_us + 10'000'000 + wanted.at_ms * 1000u);
    CHECK(error_us >= 0);
    CHECK(error_us < 1000);
}
