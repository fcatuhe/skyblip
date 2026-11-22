// DO-260B A.1.7: compact position reporting, encoded as the clause says and decoded back.
#include <cmath>
#include <cstdint>
#include <initializer_list>

#include "doctest/doctest.h"
#include "protocols/adsb/assembly.h"
#include "protocols/adsb/cpr.h"
#include "protocols/adsb/squitter.h"
#include "protocols/adsb/test/receiver.h"

using namespace skyblip;
using namespace skyblip::adsb;

namespace {

constexpr double kNmDegrees = 1852 / 111320.0;

int32_t e7(double degrees) { return static_cast<int32_t>(std::lround(degrees * 1e7)); }

CprPosition at(double latitude, double longitude) { return {e7(latitude), e7(longitude)}; }

double metres_between(const CprPosition& one, double latitude, double longitude) {
    const double north = (one.latitude_1e7 / 1e7 - latitude) * 111320.0;
    double east_degrees = one.longitude_1e7 / 1e7 - longitude;
    if (east_degrees > 180) east_degrees -= 360;
    if (east_degrees < -180) east_degrees += 360;
    const double east = east_degrees * 111320.0 * std::cos(latitude * std::acos(-1.0) / 180);
    return std::hypot(north, east);
}

bool global(CprFormat format, double latitude, double longitude, const CprPosition& near,
            CprPosition& out) {
    const bool surface = format == CprFormat::Surface;
    const Cpr even = adsb::cpr_encode(latitude, longitude, false, surface);
    const Cpr odd = adsb::cpr_encode(latitude, longitude, true, surface);
    return cpr_global(format, odd, even, near, out);
}

}

TEST_CASE("DO-260B A.1.7: an even and an odd encoding name one place on the globe") {
    for (double latitude : {-60.0, -5.5, 0.1, 48.2, 70.3}) {
        CprPosition position;
        REQUIRE(global(CprFormat::Airborne, latitude, 8.2, CprPosition{}, position));
        CHECK(metres_between(position, latitude, 8.2) < 5.1);
    }
}

TEST_CASE("DO-260B A.1.7.1: each encoding alone repeats every 360 NM, and the pair does not") {
    const Cpr even = adsb::cpr_encode(48.2, 8.2, false, false);
    const double zone_degrees = 360.0 / 60;
    const Cpr shifted = adsb::cpr_encode(48.2 + zone_degrees, 8.2, false, false);
    CHECK(even.latitude_cpr == shifted.latitude_cpr);
    CprPosition position;
    REQUIRE(global(CprFormat::Airborne, 48.2, 8.2, CprPosition{}, position));
    CHECK(metres_between(position, 48.2, 8.2) < 5.1);
}

TEST_CASE(
    "DO-260B A.1.7.2: NZ is 15 and NL follows the closed form, 59 at the equator, 1 past 87") {
    CHECK(cpr_longitude_zones(0) == 59);
    CHECK(cpr_longitude_zones(e7(87.0)) == 2);
    CHECK(cpr_longitude_zones(e7(87.5)) == 1);
    CHECK(cpr_longitude_zones(e7(-87.5)) == 1);
    for (double latitude : {10.0, 30.0, 45.0, 48.2, 60.0, 75.0, 86.0})
        CHECK(cpr_longitude_zones(e7(latitude)) == adsb::longitude_zones_closed_form(latitude));
    CHECK(4 * 15 == 60);
}

TEST_CASE("DO-260B A.1.7.3: a position encoded as A.1.7.3 says decodes to within its step") {
    for (double longitude : {-179.9, -75.0, 0.0, 8.2, 179.9}) {
        CprPosition position;
        REQUIRE(global(CprFormat::Airborne, 48.2, longitude, CprPosition{}, position));
        CHECK(metres_between(position, 48.2, longitude) < 5.1);
    }
}

TEST_CASE("DO-260B A.1.7.4: a single encoding decodes beside a reference within 180 NM, or 45 NM") {
    CprPosition position;
    const Cpr airborne = adsb::cpr_encode(48.2, 8.2, true, false);
    REQUIRE(cpr_local(CprFormat::Airborne, airborne, at(48.2 + 170 * kNmDegrees, 8.2), position));
    CHECK(metres_between(position, 48.2, 8.2) < 5.1);
    const Cpr surface = adsb::cpr_encode(48.2, 8.2, false, true);
    REQUIRE(cpr_local(CprFormat::Surface, surface, at(48.2 + 40 * kNmDegrees, 8.2), position));
    CHECK(metres_between(position, 48.2, 8.2) < 1.3);
}

TEST_CASE("DO-260B A.1.7.5: the airborne local decode takes the zone nearest the reference") {
    for (bool odd : {false, true}) {
        const Cpr cpr = adsb::cpr_encode(-33.9, 151.2, odd, false);
        CprPosition position;
        REQUIRE(cpr_local(CprFormat::Airborne, cpr, at(-33.0, 150.0), position));
        CHECK(metres_between(position, -33.9, 151.2) < 5.1);
    }
}

TEST_CASE("DO-260B A.1.7.6: the surface local decode works in 90-degree zones") {
    for (bool odd : {false, true}) {
        const Cpr cpr = adsb::cpr_encode(40.64, -73.78, odd, true);
        CprPosition position;
        REQUIRE(cpr_local(CprFormat::Surface, cpr, at(40.6, -73.7), position));
        CHECK(metres_between(position, 40.64, -73.78) < 1.3);
    }
}

TEST_CASE("DO-260B A.1.7.7: an airborne pair decodes globally, unless its halves' NL differ") {
    CprPosition position;
    REQUIRE(global(CprFormat::Airborne, 52.2572, 3.9194, CprPosition{}, position));
    CHECK(metres_between(position, 52.2572, 3.9194) < 5.1);
    const Cpr even = adsb::cpr_encode(51.8930, 4.0, false, false);
    const Cpr odd = adsb::cpr_encode(51.8940, 4.0, true, false);
    CHECK(cpr_longitude_zones(e7(51.8930)) != cpr_longitude_zones(e7(51.8940)));
    CHECK_FALSE(cpr_global(CprFormat::Airborne, odd, even, CprPosition{}, position));
}

TEST_CASE("DO-260B A.1.7.8: a surface pair decodes globally into the reference's quadrant") {
    for (double longitude : {-120.0, -30.0, 60.0, 150.0}) {
        CprPosition position;
        REQUIRE(global(CprFormat::Surface, -20.0, longitude, at(-20.2, longitude + 0.3), position));
        CHECK(metres_between(position, -20.0, longitude) < 1.3);
    }
}

TEST_CASE("DO-260B A.1.7.9: positions come from a global pair first, then single frames" *
          doctest::test_suite("main")) {
    receiver::Bravo bravo;
    bravo.fly(receiver::kLatitude, receiver::kLongitude, 0, false);
    CHECK_FALSE(bravo.heard.position_valid);
    bravo.fly(receiver::kLatitude, receiver::kLongitude, 500, true);
    CHECK(bravo.heard.position_valid);
    bravo.fly(receiver::kLatitude + 0.02, receiver::kLongitude, 1500, true);
    CHECK(receiver::metres_from(bravo.heard, receiver::kLatitude + 0.02, receiver::kLongitude) <
          5.1);
}

TEST_CASE("DO-260B A.1.7.9.2: a single frame decodes against the emitter's own last position") {
    receiver::Bravo bravo;
    bravo.reference = receiver::reference_at(48.0, 8.0);
    const double latitude = 48.0 + 100 * kNmDegrees;
    bravo.fly(latitude, 8.0, 0, false);
    bravo.fly(latitude, 8.0, 500, true);
    REQUIRE(bravo.heard.position_valid);
    bravo.reference = receiver::reference_at(48.0 - 170 * kNmDegrees, 8.0);
    bravo.fly(latitude + 0.01, 8.0, 1500, false);
    CHECK(receiver::metres_from(bravo.heard, latitude + 0.01, 8.0) < 5.1);
}

TEST_CASE("DO-260B A.1.7.10: global and local decodes are each tested for reasonableness") {
    receiver::Bravo bravo;
    bravo.fly(receiver::kLatitude, receiver::kLongitude, 0, false);
    bravo.fly(receiver::kLatitude, receiver::kLongitude, 500, true);
    bravo.fly(receiver::kLatitude + 7 * kNmDegrees, receiver::kLongitude, 1500, false);
    CHECK(bravo.assembly.implausible() == 1);
}

TEST_CASE(
    "DO-260B A.1.7.10.2: a global decode past range is dropped, and a second pair confirms it") {
    receiver::Bravo far;
    far.fly(receiver::kHomeLatitude + 3.1, receiver::kHomeLongitude, 0, false);
    far.fly(receiver::kHomeLatitude + 3.1, receiver::kHomeLongitude, 500, true);
    CHECK_FALSE(far.heard.position_valid);
    CHECK(kAirborneAgreementCeilingMm == 5000);
    CHECK(kSurfaceAgreementCeilingMm == 1250);
    receiver::Bravo bravo;
    for (uint32_t at_ms = 0; at_ms <= 1500; at_ms += 500)
        bravo.fly(receiver::kLatitude, receiver::kLongitude, at_ms, (at_ms / 500) % 2 == 1);
    CHECK(bravo.heard.position_valid);
    CHECK(bravo.assembly.implausible() == 0);
}

TEST_CASE("DO-260B A.1.7.10.3: a local decode moves 6, 2.5 or 0.75 NM at most in 30 s" *
          doctest::test_suite("main")) {
    CHECK(kAirborneJumpCeilingM == 6 * 1852);
    CHECK(kTransitionJumpCeilingM == 5 * 1852 / 2);
    CHECK(kSurfaceJumpCeilingM == 3 * 1852 / 4);
    CHECK(kTrackJumpWindowMs == 30000);
    receiver::Bravo bravo;
    bravo.fly(receiver::kLatitude, receiver::kLongitude, 0, false);
    bravo.fly(receiver::kLatitude, receiver::kLongitude, 500, true);
    REQUIRE(bravo.hear(
        adsb::surface_position(receiver::kAddress, 0, receiver::kLatitude + 3 * kNmDegrees,
                               receiver::kLongitude, false),
        1500));
    CHECK(receiver::metres_from(bravo.heard, receiver::kLatitude, receiver::kLongitude) < 5.1);
}
