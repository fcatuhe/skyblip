// ADS-L 4 SRD-860 issue 2 C.5 through a lost PPS: the Go keeps its second for the holdover and
// listens only.
#include <cstdint>

#include "core/timing/slot.h"
#include "core/timing/transmit_gate.h"
#include "doctest/doctest.h"
#include "protocols/adsl/transmit.h"
#include "simulator/simulator.h"
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

// A burst keys at most window B's close after its edge, as on a lock, never on a later second.
TEST_CASE("rf holdover: a Go that loses its PPS keys nothing past the last edge's second") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(120);
    const uint32_t settled_ms = past_settling(simulator);
    run_on(simulator, settled_ms, 5000);

    const uint32_t lost_ms = settled_ms + 5010;
    const uint64_t last_edge_us = uint64_t{lost_ms} / 1000 * 1'000'000;
    simulator.world().set_pps_locked(false);
    run_on(simulator, lost_ms, 30000);

    const simulator::Air& air = simulator.world().air();
    const uint64_t silent_us = last_edge_us + uint64_t{timing::kPpsEdgeMaxAgeMs} * 1000;
    CHECK(positions_between(air, uint64_t{lost_ms} * 1000, silent_us) >= 1);
    CHECK(positions_between(air, silent_us, uint64_t{lost_ms + 30000} * 1000) == 0);
    CHECK(simulator.product().state().transmit.unmet() == timing::NoTx::Pps);
}

// C.5: the held edge keeps the windows where the neighbours transmit, so they are still heard.
TEST_CASE("rf holdover: a Go keeps hearing traffic through the holdover, receive only") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(120);
    simulator.world().add_aircraft(1500, 500, 0, 30, 180);
    const uint32_t settled_ms = past_settling(simulator);

    const uint32_t lost_ms = settled_ms + 10;
    simulator.world().set_pps_locked(false);
    run_on(simulator, lost_ms, 5000);
    const uint32_t heard = simulator.product().state().air.rx_ok;
    simulator.world().air().clear();
    run_on(simulator, lost_ms + 5005, 40000);

    CHECK(timing::in_pps_holdover(simulator.product().state().clock));
    CHECK(simulator.product().state().air.rx_ok >= heard + 20);
    CHECK(count_payloads(simulator.world().air(), adsl::Packet::kPayloadTypePosition) == 0);
}

TEST_CASE(
    "rf holdover: past the holdover the Go still transmits nothing, and resumes on the edge") {
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

// The held edge is not on the uptime's second, and the dwells run on the edge's.
TEST_CASE("rf holdover: on a held second the dwells run from the held edge, none allowed a burst") {
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
    CHECK_FALSE(pass.state.rf.plan.listen_only);
    CHECK_FALSE(pass.state.rf.plan.tx_allowed);
}

// The device calls a pulse locked for 3 s past its last edge, and the second has to move on
// meanwhile.
TEST_CASE(
    "rf holdover: in the 3 s a lost pulse still reads locked, the second counts on from the edge") {
    Pass pass;
    REQUIRE(pass.begin() == Status::Ok);
    pass.poll_clock(12'100'000);
    pass.state.clock.utc_s = 1000;
    pass.state.clock.utc_edge_us = 12'000'000;
    pass.platform.clock().set_microseconds(12'500'000);
    pass.platform.pps().set_locked(false);

    pass.poll_clock(13'200'000);
    CHECK(pass.state.clock.pps_locked);
    CHECK(pass.state.traffic_now_s(13200) == 1001);
    pass.poll_clock(14'600'000);
    CHECK(pass.state.clock.pps_locked);
    CHECK(pass.state.traffic_now_s(14600) == 1002);
    pass.poll_clock(15'100'000);
    CHECK_FALSE(pass.state.clock.pps_locked);
    CHECK(pass.state.traffic_now_s(15100) == 1003);
}
