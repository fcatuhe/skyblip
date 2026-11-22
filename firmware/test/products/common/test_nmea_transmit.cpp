// PFLAU's TX field is the transmit gate's verdict, on a Go whose receiver solves at 2 Hz.
#include <cstdint>
#include <string>

#include "core/events/link.h"
#include "core/timing/slot.h"
#include "doctest/doctest.h"
#include "simulator/simulator.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

// INFO: fc 19nov26 the TX field of the newest PFLAU the phone heard, empty before the first
std::string last_pflau_tx(simulator::Simulator& simulator) {
    std::string stream;
    for (const auto& frame : simulator.platform().link().sent)
        if (frame.endpoint == events::Endpoint::Nmea) stream += frame.bytes;
    const size_t at = stream.rfind("$PFLAU,");
    if (at == std::string::npos) return std::string();
    const size_t rx_end = stream.find(',', at + 7);
    const size_t tx_end = stream.find(',', rx_end + 1);
    return stream.substr(rx_end + 1, tx_end - rx_end - 1);
}

}  // namespace

// The TX field is what an app shows as transmitting, and a device can hear all and say nothing.
TEST_CASE("nmea: PFLAU's TX field is the transmit gate's, so a silent device admits it") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(120);
    simulator.world().connect_companion();
    const uint32_t settled_ms = past_settling(simulator);
    run_on(simulator, settled_ms, 2000);
    REQUIRE(simulator.product().state().transmit.transmits());
    CHECK(last_pflau_tx(simulator) == "1");

    simulator.world().set_pps_locked(false);
    const uint32_t lost_ms = settled_ms + 2010;
    run_on(simulator, lost_ms, timing::kPpsEdgeMaxAgeMs + 1000);
    REQUIRE_FALSE(simulator.product().state().transmit.transmits());
    CHECK(last_pflau_tx(simulator) == "0");
    CHECK(simulator.product().state().own.fix_valid);
}
