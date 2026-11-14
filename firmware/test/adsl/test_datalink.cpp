// ADS-L 4 SRD-860 issue 2 Subpart D, the data link: media access as flown.
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "protocols/adsl/media_access.h"
#include "simulator/simulator.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

// Own-ship's bursts with its channel held busy for `held_ms` from the top of a cycle.
std::vector<simulator::AirRecord> held_from_cycle_start(int speed_kt, uint32_t held_ms,
                                                        uint64_t& held_from_us, uint32_t& forced) {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    std::strncpy(simulator.product().settings().callsign, "D-KXYZ", go::kCallsignBytes - 1);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(speed_kt);
    uint32_t now_ms = past_settling(simulator);
    const timing::Transmitter& transmitter = simulator.product().radio().transmitter();
    const auto cycle_second = [&]() {
        const uint32_t second = simulator.product().state().traffic_now_s(now_ms);
        return (second + timing::Transmitter::kCycleS - transmitter.cycle_start()) %
               timing::Transmitter::kCycleS;
    };
    const uint32_t ceiling_ms = now_ms + 30000;
    while (now_ms < ceiling_ms && !(now_ms % 1000 == 0 && cycle_second() == 0))
        simulator.step(++now_ms);
    REQUIRE(now_ms % 1000 == 0);

    held_from_us = static_cast<uint64_t>(now_ms) * 1000;
    const int8_t held_dbm[1] = {-50};
    models::Sx1262& chip = simulator.platform().chips().radio;
    chip.set_rssi_sequence(held_dbm, 1);
    run_on(simulator, now_ms + 1, held_ms - 1, 1);
    chip.set_rssi_sequence(held_dbm, 0);
    forced = simulator.product().state().air.tx_forced;

    std::vector<simulator::AirRecord> sent;
    const simulator::Air& air = simulator.world().air();
    for (int i = 0; i < air.record_count(); i++)
        if (air.record_at(i).event == simulator::AirEvent::Tx &&
            air.record_at(i).at_us >= held_from_us)
            sent.push_back(air.record_at(i));
    return sent;
}

}  // namespace

// LBT itself is EN 300 220-1's (test/en300220/): this reads the clause's own two numbers.
TEST_CASE("ADS-L.4.SRD860.D.3: a held channel defers, forces after 3000 ms, then keeps 2000 ms") {
    uint64_t held_from_us = 0;
    uint32_t forced = 0;
    const std::vector<simulator::AirRecord> ground =
        held_from_cycle_start(0, 12000, held_from_us, forced);
    CHECK(forced >= 2);
    REQUIRE(ground.size() >= 2);
    CHECK(ground.size() <= forced);
    CHECK(ground[0].at_us - held_from_us >= 3000000);
    for (size_t i = 1; i < ground.size(); i++)
        CHECK(ground[i].at_us - ground[i - 1].at_us >= 2000000);

    // Airborne a position is never forced: the next second's covers it.
    const std::vector<simulator::AirRecord> airborne =
        held_from_cycle_start(50, 2000, held_from_us, forced);
    CHECK(airborne.empty());
    CHECK(forced == 0);
}
