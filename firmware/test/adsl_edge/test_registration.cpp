// The name a Go flies in OGN's payload 66, ahead of the ADS-L issue that will define it.
#include <cstdint>
#include <cstring>
#include <string>

#include "core/protocol/callsign.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/settings.h"
#include "protocols/adsl/physical.h"
#include "protocols/adsl/transmit.h"
#include "protocols/adsl_edge/registration.h"
#include "simulator/simulator.h"
#include "test/support/settings_link.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

TEST_CASE("adsl edge: the name goes out as OGN's registration record, and reads back" *
          doctest::test_suite("main")) {
    adsl::Packet packet{};
    REQUIRE(protocol::from_own_callsign(packet, 0x123456, 58, "D-KXYZ"));
    CHECK(adsl_edge::is_registration(packet));
    CHECK(packet.address() == 0x123456u);
    CHECK(packet.address_mapping_table() == 58);
    CHECK(packet.protocol_version() == 1);

    char name[adsl_edge::kInformationTextBytes + 1] = {0};
    CHECK(protocol::callsign_of(packet, name, sizeof(name)) == 6);
    CHECK(std::string(name) == "D-KXYZ");
}

TEST_CASE("adsl edge: a registration that is not printable is refused, not drawn") {
    adsl::Packet packet{};
    protocol::from_own_callsign(packet, 0x123456, 58, "D-KXYZ");
    adsl_edge::information_text(packet)[3] = 0x07;

    char name[adsl_edge::kInformationTextBytes + 1] = {0};
    CHECK(protocol::callsign_of(packet, name, sizeof(name)) == 0);
    CHECK(name[0] == 0);

    // And a telemetry record that is not the registration one is not a name either.
    adsl::Packet other{};
    protocol::from_own_callsign(other, 0x123456, 58, "D-KXYZ");
    adsl_edge::set_information_header(other, 0, 5);
    CHECK_FALSE(adsl_edge::is_registration(other));
    CHECK(protocol::callsign_of(other, name, sizeof(name)) == 0);
}

TEST_CASE("adsl edge: a registration with a comma is refused, not split into PFLAA") {
    adsl::Packet packet{};
    protocol::from_own_callsign(packet, 0x123456, 58, "D-KXYZ");
    adsl_edge::information_text(packet)[1] = ',';

    char name[adsl_edge::kInformationTextBytes + 1] = {0};
    CHECK(protocol::callsign_of(packet, name, sizeof(name)) == 0);
    CHECK(name[0] == 0);
}

// Out of the direct slot ADS-L Issue 2 defines, on B, in the even second whose position is on A.
TEST_CASE("adsl edge: the name goes in window B's tail, past the direct slot, on B" *
          doctest::test_suite("main")) {
    timing::ClockState clock{};
    clock.utc_valid = true;
    clock.pps_locked = true;
    const timing::SlotPlan slot1 = timing::Scheduler::plan(timing::kWindowBMs.start_ms, clock);

    int seen = 0;
    for (uint32_t address = 1; address < 64; address++) {
        timing::Transmitter tx;
        tx.configure(address * 0x9E37u);
        for (uint32_t utc_s = 0; utc_s < 40; utc_s++) {
            const timing::Transmitter::Attempt attempt =
                tx.attempt(slot1, utc_s, 1000, true, 0, timing::Transmitter::Payload::Callsign);
            if (!attempt.go) continue;
            seen++;
            CHECK_FALSE(timing::Scheduler::in_direct_slot(attempt.at_ms));
            CHECK(attempt.at_ms >= adsl::kDirectEndMs);
            CHECK(attempt.at_ms + static_cast<int>(timing::Transmitter::kAirTimeMs) <=
                  timing::kWindowBMs.end_ms);
            CHECK(attempt.frequency_hz == adsl::kMBand1Hz);
            CHECK(utc_s % 2 == 0);
        }
    }
    CHECK(seen > 0);
}

// A receiver files the name under the same target as the Traffic, so it follows the ICAO address.
TEST_CASE("adsl edge: the name goes under the ICAO address while one is set" *
          doctest::test_suite("main")) {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    go::Settings& settings = simulator.product().settings();
    std::strncpy(settings.callsign, "D-KXYZ", go::kCallsignBytes - 1);
    REQUIRE(apply_patch(settings, "{\"aircraft_address\":3934737}") == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    run_on(simulator, past_settling(simulator), 21000);

    int named = 0;
    const simulator::Air& air = simulator.world().air();
    for (int i = 0; i < air.record_count(); i++) {
        adsl::Packet packet{};
        if (!packet_of(air.record_at(i), packet)) continue;
        if (!adsl_edge::is_registration(packet)) continue;
        named++;
        CHECK(packet.address_mapping_table() == 5);
        CHECK(packet.address() == 0x3C0A11u);
    }
    CHECK(named >= 2);
}
