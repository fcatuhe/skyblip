// CPR from DF17 positions to coordinates, pinned to the Riddle's examples and DO-260B Table 2-130.
#include <cmath>
#include <cstdint>
#include <initializer_list>

#include "doctest/doctest.h"
#include "protocols/adsb/cpr.h"
#include "protocols/adsb/message.h"
#include "protocols/adsb/test/receiver.h"

using namespace skyblip;
using namespace skyblip::adsb;
using skyblip::wire::Status;

namespace {

adsb::Message heard(const char* text) {
    const AdsbFrame frame = frame_of(text);
    uint8_t chips[adsb::kChipBytes];
    adsb_chips(frame.bytes, chips);
    adsb::Message message;
    REQUIRE(adsb::receive(chips, sizeof(chips), adsb::Reception{}, message) == Status::Ok);
    return message;
}

int32_t e7(double degrees) { return static_cast<int32_t>(std::lround(degrees * 1e7)); }

adsb::CprPosition at(double latitude, double longitude) { return {e7(latitude), e7(longitude)}; }

adsb::CprPosition local_of(adsb::CprFormat format, const adsb::Cpr& cpr,
                           const adsb::CprPosition& reference) {
    adsb::CprPosition position;
    REQUIRE(adsb::cpr_local(format, cpr, reference, position));
    return position;
}

double metres_between(const adsb::CprPosition& one, double latitude, double longitude) {
    const double north = (one.latitude_1e7 / 1e7 - latitude) * 111320.0;
    double east_degrees = one.longitude_1e7 / 1e7 - longitude;
    if (east_degrees > 180) east_degrees -= 360;
    if (east_degrees < -180) east_degrees += 360;
    const double east = east_degrees * 111320.0 * std::cos(latitude * std::acos(-1.0) / 180);
    return std::hypot(north, east);
}

}

TEST_CASE("adsb cpr: the Riddle's airborne pair decodes globally to 52.25720 N 3.91937 E") {
    const adsb::Cpr even = heard("8d40621d58c382d690c8ac2863a7").airborne_position.cpr;
    const adsb::Cpr odd = heard("8d40621d58c386435cc412692ad6").airborne_position.cpr;
    adsb::CprPosition position;
    REQUIRE(adsb::cpr_global(adsb::CprFormat::Airborne, even, odd, adsb::CprPosition{}, position));
    CHECK(position.latitude_1e7 == e7(52.2572021484375));
    CHECK(position.longitude_1e7 == e7(3.91937255859375));
}

TEST_CASE("adsb cpr: the Riddle's airborne frame decodes locally beside its reference") {
    const adsb::Cpr even = heard("8d40621d58c382d690c8ac2863a7").airborne_position.cpr;
    const adsb::CprPosition position = local_of(adsb::CprFormat::Airborne, even, at(52.258, 3.918));
    CHECK(position.latitude_1e7 == e7(52.2572021484375));
    CHECK(position.longitude_1e7 == e7(3.91937255859375));
}

TEST_CASE("adsb cpr: the Riddle's surface pair takes the quadrant of its reference") {
    const adsb::Cpr even = heard("8c4841753aab238733c8cd4020b1").surface_position.cpr;
    const adsb::Cpr odd = heard("8c4841753a8a35323faebdac702d").surface_position.cpr;
    REQUIRE_FALSE(even.odd);
    adsb::CprPosition position;
    REQUIRE(adsb::cpr_global(adsb::CprFormat::Surface, odd, even, at(51.990, 4.375), position));
    CHECK(position.latitude_1e7 == e7(52.320607072215964));
    CHECK(position.longitude_1e7 == e7(4.734734671456474));
}

TEST_CASE("adsb cpr: a surface pair in the south-west quadrant decodes beside a reference there") {
    const adsb::Cpr even = adsb_cpr_encode(-34.8222, -58.5358, false, true);
    const adsb::Cpr odd = adsb_cpr_encode(-34.8222, -58.5358, true, true);
    adsb::CprPosition position;
    REQUIRE(adsb::cpr_global(adsb::CprFormat::Surface, even, odd, at(-34.6, -58.4), position));
    CHECK(metres_between(position, -34.8222, -58.5358) < 1.3);
    REQUIRE(adsb::cpr_global(adsb::CprFormat::Surface, even, odd, at(52.0, 4.0), position));
    CHECK(position.latitude_1e7 > 0);
    CHECK(position.longitude_1e7 > 0);
}

TEST_CASE("adsb cpr: the Riddle's surface frame decodes locally beside the track it continues") {
    const adsb::Cpr odd = heard("8c4841753a9a153237aef0f275be").surface_position.cpr;
    const adsb::CprPosition position =
        local_of(adsb::CprFormat::Surface, odd, at(52.320607, 4.734735));
    CHECK(position.latitude_1e7 == e7(52.32056051997815));
    CHECK(position.longitude_1e7 == e7(4.735735212053571));
}

// A Table 2-130 row brackets a transition: the lower latitude keeps the count, the upper loses one.
TEST_CASE("adsb cpr: the longitude zones change across DO-260B Table 2-130's latitudes") {
    struct Row {
        double low;
        double up;
        int zones;
    };
    const Row rows[] = {
        {10.470463, 10.470474, 59}, {29.911354, 29.911366, 52}, {53.095161, 53.095173, 36},
        {86.535370, 86.535381, 3},  {86.999999, 87.000011, 2},
    };
    for (const Row& row : rows) {
        CAPTURE(row.low);
        CHECK(adsb::cpr_longitude_zones(e7(row.low)) == row.zones);
        CHECK(adsb::cpr_longitude_zones(-e7(row.low)) == row.zones);
        CHECK(adsb::cpr_longitude_zones(e7(row.up)) == row.zones - 1);
    }
    CHECK(adsb::cpr_longitude_zones(0) == 59);
    CHECK(adsb::cpr_longitude_zones(e7(87.0)) == 2);
    CHECK(adsb::cpr_longitude_zones(e7(90.0)) == 1);
}

TEST_CASE("adsb cpr: the zone table agrees with the closed form away from its transitions") {
    for (double latitude = -89.95; latitude < 90; latitude += 0.1) {
        CAPTURE(latitude);
        CHECK(adsb::cpr_longitude_zones(e7(latitude)) == adsb_longitude_zones(latitude));
    }
}

// A pair across a transition latitude has no common longitude zone count, so A.1.7.7 d waits.
TEST_CASE("adsb cpr: a pair that straddles a transition latitude is not decoded") {
    const adsb::Cpr even = adsb_cpr_encode(51.8930, 4.0, false, false);
    const adsb::Cpr odd = adsb_cpr_encode(51.8940, 4.0, true, false);
    adsb::CprPosition position;
    CHECK_FALSE(
        adsb::cpr_global(adsb::CprFormat::Airborne, odd, even, adsb::CprPosition{}, position));

    const adsb::Cpr later = adsb_cpr_encode(51.8945, 4.0, false, false);
    REQUIRE(adsb::cpr_global(adsb::CprFormat::Airborne, later, odd, adsb::CprPosition{}, position));
    CHECK(metres_between(position, 51.8945, 4.0) < 5.1);
}

TEST_CASE("adsb cpr: two halves of one parity are no pair") {
    const adsb::Cpr even = adsb_cpr_encode(48.1, 8.1, false, false);
    adsb::CprPosition position;
    CHECK_FALSE(
        adsb::cpr_global(adsb::CprFormat::Airborne, even, even, adsb::CprPosition{}, position));
}

// The encoder is floating point and the decoder integer, so agreeing everywhere is a real check.
TEST_CASE("adsb cpr: positions round the globe come back within the encoding's resolution") {
    const double places[][2] = {{48.1, 8.1},    {-33.9, 151.2}, {64.1, -21.9},    {0.0, 0.0},
                                {-0.3, -179.9}, {40.6, -73.8},  {-54.8, -68.3},   {35.5, 139.8},
                                {78.2, 15.6},   {-77.8, 166.7}, {51.47, -0.0001}, {10.4704, 120.0}};
    for (const auto& place : places) {
        const double latitude = place[0], longitude = place[1];
        CAPTURE(latitude);
        CAPTURE(longitude);
        const adsb::Cpr even = adsb_cpr_encode(latitude, longitude, false, false);
        const adsb::Cpr odd = adsb_cpr_encode(latitude, longitude, true, false);
        adsb::CprPosition position;
        REQUIRE(
            adsb::cpr_global(adsb::CprFormat::Airborne, odd, even, adsb::CprPosition{}, position));
        CHECK(metres_between(position, latitude, longitude) < 5.1);
        REQUIRE(
            adsb::cpr_global(adsb::CprFormat::Airborne, even, odd, adsb::CprPosition{}, position));
        CHECK(metres_between(position, latitude, longitude) < 5.1);

        const adsb::CprPosition near = at(latitude + 1.0, longitude - 1.0);
        CHECK(metres_between(local_of(adsb::CprFormat::Airborne, odd, near), latitude, longitude) <
              5.1);

        const adsb::Cpr surface_even = adsb_cpr_encode(latitude, longitude, false, true);
        const adsb::Cpr surface_odd = adsb_cpr_encode(latitude, longitude, true, true);
        REQUIRE(
            adsb::cpr_global(adsb::CprFormat::Surface, surface_even, surface_odd, near, position));
        CHECK(metres_between(position, latitude, longitude) < 1.3);
        const adsb::CprPosition beside = at(latitude + 0.3, longitude + 0.3);
        CHECK(metres_between(local_of(adsb::CprFormat::Surface, surface_even, beside), latitude,
                             longitude) < 1.3);
    }
}

// A rounding tie once put the two decodes of one frame 1e-7 degree apart: they must round alike.
TEST_CASE("adsb cpr: a global decode and the local decode of its newest frame are identical") {
    const double places[][2] = {{-5.98828125, -75.0}, {62.147250, -46.582038}};
    uint64_t seed = 0x2545F4914F6CDD1DULL;
    const auto next = [&seed](double span) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return (static_cast<double>(seed >> 11) / 9007199254740992.0 - 0.5) * span;
    };
    int mismatches = 0;
    for (int i = 0; i < 20002; i++) {
        const double latitude = i < 2 ? places[i][0] : next(170.0);
        const double longitude = i < 2 ? places[i][1] : next(360.0);
        for (bool surface : {false, true}) {
            const adsb::CprFormat format =
                surface ? adsb::CprFormat::Surface : adsb::CprFormat::Airborne;
            for (bool odd_newest : {false, true}) {
                const adsb::Cpr newest = adsb_cpr_encode(latitude, longitude, odd_newest, surface);
                const adsb::Cpr other = adsb_cpr_encode(latitude, longitude, !odd_newest, surface);
                adsb::CprPosition global;
                if (!adsb::cpr_global(format, newest, other, at(latitude, longitude), global))
                    continue;
                const adsb::CprPosition local = local_of(format, newest, global);
                if (local.latitude_1e7 != global.latitude_1e7 ||
                    local.longitude_1e7 != global.longitude_1e7)
                    mismatches++;
            }
        }
    }
    CHECK(mismatches == 0);
}

// fuzz_adsb_assembly found it: a frame decoded against a reference at the pole landed past it.
TEST_CASE("adsb cpr: a single frame that decodes past a pole is no position") {
    const adsb::Cpr polar = heard("8840006e6e1a00006e006eaa4853").airborne_position.cpr;
    adsb::CprPosition position;
    CHECK_FALSE(adsb::cpr_local(adsb::CprFormat::Airborne, polar, at(90.0, 0.0), position));
    CHECK(adsb::cpr_local(adsb::CprFormat::Airborne, polar, at(84.0, 0.0), position));
    CHECK(position.latitude_1e7 <= 900000000);
}
