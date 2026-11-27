// A Bravo dates its ADS-B on its own UTC second, which must keep counting when its receiver stops.
#include <cstdint>

#include "core/comms/peer.h"
#include "core/model/aircraft.h"
#include "doctest/doctest.h"
#include "simulator/paired_simulator.h"
#include "test/support/bravo_flight.h"

using namespace skyblip;

namespace {

struct PairedFlight {
    simulator::PairedSimulator simulator;
    uint32_t now_ms{0};

    PairedFlight() {
        REQUIRE(simulator.setup() == Status::Ok);
        REQUIRE(simulator.load_file("scenarios/paired.json"));
    }

    void run_until(uint32_t until_ms) {
        for (; now_ms <= until_ms; now_ms += simulator::PairedSimulator::kStepMs)
            simulator.step(now_ms);
    }

    const bus::State& go() { return simulator.product().state(); }
    const bus::State& bravo() { return simulator.bravo().state(); }
    int relayed() { return go().traffic.count_of(model::Source::Adsb); }

    // The receiver stops talking and answers no reconfiguration, as a hung or unplugged part.
    void silence_bravo_gnss() {
        simulator.bravo_platform().gnss().answers = false;
        simulator.bravo_platform().gnss().solution_period_ms = UINT32_MAX;
    }
};

}  // namespace

TEST_CASE("bravo clock: a Bravo whose GNSS goes quiet with its pulse kept still relays ADS-B") {
    PairedFlight flight;
    flight.run_until(10000);
    REQUIRE(flight.go().bravo == comms::BravoState::Live);
    REQUIRE(flight.relayed() >= 2);

    flight.silence_bravo_gnss();
    flight.run_until(30000);
    CHECK(flight.go().bravo == comms::BravoState::Live);
    CHECK(flight.relayed() >= 2);
}

// A Bravo with UTC and no pulse, as one solving time without a fix, dates on its last solution.
TEST_CASE("bravo clock: a Bravo whose GNSS goes quiet without a pulse counts on and relays ADS-B") {
    PairedFlight flight;
    flight.simulator.bravo_platform().pps().set_locked(false);
    flight.run_until(10000);
    REQUIRE(flight.go().bravo == comms::BravoState::Live);
    REQUIRE(flight.relayed() >= 2);

    flight.silence_bravo_gnss();
    flight.run_until(15000);
    const uint32_t bravo_s_before = flight.bravo().traffic_now_s(flight.now_ms);
    flight.run_until(25000);
    CHECK(flight.bravo().traffic_now_s(flight.now_ms) - bravo_s_before >= 9);
    CHECK(flight.go().bravo == comms::BravoState::Live);
    CHECK(flight.relayed() >= 2);
}

TEST_CASE("bravo clock: a Bravo whose GNSS and pulse stop together relays past the holdover") {
    PairedFlight flight;
    flight.run_until(10000);
    REQUIRE(flight.go().bravo == comms::BravoState::Live);
    REQUIRE(flight.relayed() >= 2);

    flight.silence_bravo_gnss();
    flight.simulator.bravo_platform().pps().set_locked(false);
    flight.run_until(40000);
    CHECK(flight.relayed() >= 2);
    flight.run_until(90000);
    CHECK(flight.go().bravo == comms::BravoState::Live);
    CHECK(flight.relayed() >= 2);
}

// A Bravo built before utc_source keeps the second of its last solution once its receiver stops.
TEST_CASE("bravo clock: a Bravo with no utc_source whose receiver is silent is not live") {
    Flight flight;
    flight.tango.bravo.health.status_fields =
        "\"charging\":false,\"power_level\":\"normal\",\"gnss_stage\":\"fixed\",\"satellites\":9";
    flight.fly(3);
    REQUIRE(flight.state().bravo == comms::BravoState::Live);

    flight.tango.bravo.health.status_fields =
        "\"charging\":false,\"power_level\":\"normal\",\"gnss_stage\":\"silent\"";
    flight.fly(2);
    CHECK(flight.state().bravo == comms::BravoState::NoUtc);

    flight.tango.bravo.health.status_fields =
        "\"charging\":false,\"power_level\":\"normal\",\"gnss_stage\":\"silent\","
        "\"utc_source\":\"own\"";
    flight.fly(2);
    CHECK(flight.state().bravo == comms::BravoState::Live);
}
