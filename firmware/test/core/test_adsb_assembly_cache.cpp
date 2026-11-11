// The ADS-B cache: which 100 aircraft it holds, which it forgets, and what a newcomer costs.
#include <cstdint>

#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/protocol/adsb.h"
#include "core/protocol/adsb_assembly.h"
#include "core/units/units.h"
#include "doctest/doctest.h"
#include "test/support/adsb_frames.h"
#include "test/support/adsb_receiver.h"

using namespace skyblip;
using namespace skyblip::protocol;
using namespace skyblip::adsb_receiver;

// A crowd larger than the cache keeps the aircraft nearest the reference, whatever came first.
TEST_CASE("adsb assembly: 300 aircraft leave the nearest 100 tracked and count who lost a place") {
    REQUIRE(AdsbAssembly::kMaxAircraft == 100);
    Bravo bravo;
    const auto latitude_of = [](int i) { return kHomeLatitude + (i + 1) * 0.009; };
    const auto address_of = [](int i) { return 0x400000u + static_cast<uint32_t>(i); };
    for (int i = 299; i >= 0; i--) {
        AdsbMessage message;
        REQUIRE(adsb_hear(adsb_airborne_position(address_of(i), 5, latitude_of(i), kHomeLongitude,
                                                 false, 3000),
                          message) == Status::Ok);
        bravo.assembly.fold(message, static_cast<uint32_t>(300 - i), events::Stamp{},
                            bravo.reference, bravo.heard);
    }
    CHECK(bravo.assembly.count() == 100);
    CHECK(bravo.assembly.evictions() == 200);

    int tracked = 0;
    for (int i = 299; i >= 0; i--) {
        AdsbMessage message;
        REQUIRE(adsb_hear(adsb_airborne_position(address_of(i), 5, latitude_of(i), kHomeLongitude,
                                                 true, 3000),
                          message) == Status::Ok);
        const bool kept = bravo.assembly.fold(message, static_cast<uint32_t>(1000 - i),
                                              events::Stamp{}, bravo.reference, bravo.heard);
        CHECK(kept == (i < 100));
        if (kept && bravo.heard.position_valid) tracked++;
    }
    CHECK(tracked == 100);
    CHECK(bravo.assembly.refusals() == 200);
    CHECK(bravo.assembly.count() == 100);
}

TEST_CASE("adsb assembly: an aircraft silent for 250 s is forgotten, and its place is free") {
    Bravo bravo;
    for (uint32_t i = 0; i < AdsbAssembly::kMaxAircraft; i++)
        bravo.fly(kLatitude, kLongitude, 0, false, 3000, 0x400000u + i);
    REQUIRE(bravo.hear(adsb_identification(0x400000u, kNoCapability, 3, 1, "DGLID"), 0));
    REQUIRE(bravo.heard.aircraft_category == 4);

    bravo.fly(kLatitude, kLongitude, kAdsbAircraftForgetMs + 1, false, 3000, 0x500000u);
    CHECK(bravo.assembly.count() == AdsbAssembly::kMaxAircraft);
    CHECK(bravo.assembly.evictions() == 0);
    CHECK(bravo.assembly.refusals() == 0);

    bravo.fly(kLatitude, kLongitude, kAdsbAircraftForgetMs + 2, false, 3000, 0x400001u);
    REQUIRE(bravo.hear(adsb_ground_velocity(0x400000u, kNoCapability, 0, 80, 0, 0),
                       kAdsbAircraftForgetMs + 3));
    CHECK(bravo.heard.aircraft_category == 0);
}

// The ranges are first taken at 48.00 N, then the receiver flies 6.7 km north and the order flips.
TEST_CASE("adsb assembly: a receiver that moves ranks the cache from where it is") {
    Bravo bravo;
    bravo.reference = reference_at(48.0, 8.0);
    const uint32_t beside = 0x400000u;
    bravo.fly(48.0, 8.0, 0, false, 3000, beside);
    for (uint32_t i = 1; i < AdsbAssembly::kMaxAircraft; i++)
        bravo.fly(48.03, 8.0, i, false, 3000, beside + i);
    AdsbMessage far;
    REQUIRE(adsb_hear(adsb_airborne_position(0x500000u, kNoCapability, 48.05, 8.0, false, 3000),
                      far) == Status::Ok);
    REQUIRE_FALSE(bravo.assembly.fold(far, 1000, events::Stamp{}, bravo.reference, bravo.heard));

    bravo.reference = reference_at(48.06, 8.0);
    AdsbMessage near;
    REQUIRE(adsb_hear(adsb_airborne_position(0x500001u, kNoCapability, 48.06, 8.0, false, 3000),
                      near) == Status::Ok);
    REQUIRE(bravo.assembly.fold(near, 2000, events::Stamp{}, bravo.reference, bravo.heard));
    CHECK(bravo.assembly.evictions() == 1);
    CHECK_FALSE(bravo.hear(adsb_ground_velocity(beside, kNoCapability, 0, 80, 0, 0), 3000));
    CHECK(bravo.hear(adsb_ground_velocity(beside + 1, kNoCapability, 0, 80, 0, 0), 3000));
}

TEST_CASE("adsb assembly: with no reference a full cache gives the newcomer the quietest place") {
    Bravo bravo;
    for (uint32_t i = 0; i < AdsbAssembly::kMaxAircraft; i++)
        bravo.fly(kLatitude, kLongitude, i, false, 3000, 0x400000u + i);
    bravo.reference = AdsbReference{};
    for (uint32_t i = 0; i < 50; i++)
        CHECK(bravo.hear(adsb_airborne_position(0x500000u + i, kNoCapability, kLatitude, kLongitude,
                                                false, 3000),
                         1000 + i));
    CHECK(bravo.assembly.evictions() == 50);
    CHECK(bravo.assembly.refusals() == 0);
}

// Near the uptime wrap the time of a half never heard, zero, once looked newer than a held one.
TEST_CASE("adsb assembly: a half never heard is no newer than a held one, across the uptime wrap") {
    for (uint32_t base_ms : {0u, 0xFFFF0000u}) {
        CAPTURE(base_ms);
        Bravo bravo;
        bravo.reference = reference_at(48.0, 8.0);
        for (uint32_t i = 0; i < AdsbAssembly::kMaxAircraft; i++)
            bravo.fly(48.01, 8.0, base_ms + i, false, 3000, 0x400000u + i);
        AdsbMessage message;
        REQUIRE(adsb_hear(adsb_airborne_position(0x500000u, kNoCapability, 49.0, 8.0, false, 3000),
                          message) == Status::Ok);
        CHECK_FALSE(bravo.assembly.fold(message, base_ms + 1000, events::Stamp{}, bravo.reference,
                                        bravo.heard));
        CHECK(bravo.assembly.evictions() == 0);
    }
}

// Aged every pass, the cache holds no time old enough to wrap the clock.
TEST_CASE("adsb assembly: an aircraft silent for 250 s is aged out, across the uptime wrap") {
    for (uint32_t base_ms : {0u, 0xFFFFF000u}) {
        CAPTURE(base_ms);
        Bravo bravo;
        bravo.fly(kLatitude, kLongitude, base_ms, false);
        bravo.assembly.age_out(base_ms + kAdsbAircraftForgetMs);
        CHECK(bravo.assembly.count() == 1);
        bravo.assembly.age_out(base_ms + kAdsbAircraftForgetMs + 1);
        CHECK(bravo.assembly.count() == 0);
        CHECK(bravo.assembly.evictions() == 0);
    }
}

// The cache keeps each range until the receiver or the aircraft moves, and no longer.
TEST_CASE("adsb assembly: an aircraft that flies away is ranked from where it is now") {
    Bravo bravo;
    bravo.reference = reference_at(48.0, 8.0);
    for (uint32_t i = 1; i < AdsbAssembly::kMaxAircraft; i++)
        bravo.fly(48.01, 8.0, i, false, 3000, 0x400000u + i);
    bravo.fly(48.001, 8.0, 100, false, 3000, 0x400000u);
    AdsbMessage far;
    REQUIRE(adsb_hear(adsb_airborne_position(0x500001u, kNoCapability, 49.0, 8.0, false, 3000),
                      far) == Status::Ok);
    REQUIRE_FALSE(bravo.assembly.fold(far, 200, events::Stamp{}, bravo.reference, bravo.heard));
    for (uint32_t step = 0; step <= 5; step++) {
        bravo.fly(48.001 + step * 0.009, 8.0, 100 + step * 10000, false, 3000, 0x400000u);
        bravo.fly(48.001 + step * 0.009, 8.0, 600 + step * 10000, true, 3000, 0x400000u);
    }
    REQUIRE(metres_from(bravo.heard, 48.046, 8.0) < 5.1);
    AdsbMessage message;
    REQUIRE(adsb_hear(adsb_airborne_position(0x500000u, kNoCapability, 48.02, 8.0, false, 3000),
                      message) == Status::Ok);
    CHECK(bravo.assembly.fold(message, 60000, events::Stamp{}, bravo.reference, bravo.heard));
    CHECK(bravo.assembly.evictions() == 1);
}

// Each stored time expires by its own rule while the aircraft is heard, so none lives to wrap.
TEST_CASE("adsb assembly: an aircraft heard across the whole clock keeps no time from its start") {
    Bravo bravo;
    bravo.fly(kLatitude, kLongitude, 0, false);
    REQUIRE(bravo.hear(adsb_ground_velocity(kAddress, kNoCapability, 0, 80, 640, 150), 0));
    uint32_t at_ms = 0;
    for (uint64_t elapsed_ms = 200000; elapsed_ms < (uint64_t{1} << 32); elapsed_ms += 200000) {
        at_ms = static_cast<uint32_t>(elapsed_ms);
        REQUIRE(bravo.hear(adsb_identification(kAddress, kNoCapability, 3, 1, "DGLID"), at_ms));
        bravo.assembly.age_out(at_ms);
    }
    at_ms = 1000;
    bravo.assembly.age_out(at_ms);
    bravo.fly(kLatitude, kLongitude, at_ms, true, 3000);
    CHECK_FALSE(bravo.heard.position_valid);
    CHECK_FALSE(bravo.heard.speed_valid);
    CHECK_FALSE(bravo.heard.climb_valid);
    CHECK(bravo.heard.pressure_altitude_valid);
    CHECK_FALSE(bravo.heard.altitude_valid);
}
