// A solution's own instant: the quarter second its stamp names, and one fix per GGA and RMC pair.
#include <cstdio>
#include <cstring>
#include <string>

#include "core/gnss/nmea.h"
#include "doctest/doctest.h"

using namespace skyblip::gnss;

namespace {
std::string checksummed(const char* body) {
    uint8_t checksum = 0;
    for (const char* character = body; *character; character++)
        checksum ^= static_cast<uint8_t>(*character);
    char tail[4];
    std::snprintf(tail, sizeof(tail), "*%02X", checksum);
    return std::string("$") + body + tail;
}

bool parse(NmeaParser& parser, const std::string& line) {
    return parser.parse_line(line.c_str(), static_cast<int>(line.size()));
}

std::string rmc_at(const char* stamp) {
    return checksummed(
        (std::string("GPRMC,") + stamp + ",A,4807.038,N,01131.000,E,022.4,084.4,230325,,,A")
            .c_str());
}

std::string gga_at(const char* stamp) {
    return checksummed(
        (std::string("GPGGA,") + stamp + ",4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,")
            .c_str());
}
}  // namespace

// G.1.1 names a quarter second, and at 4 Hz the stamp's fraction is the only thing that says which.
TEST_CASE("gnss: the milliseconds of an RMC stamp are the solution's own") {
    NmeaParser parser;
    REQUIRE(parse(parser, rmc_at("123519.750")));
    CHECK(parser.solution().utc_ms == 750);
    CHECK(parser.solution().utc_valid);

    REQUIRE(parse(parser, rmc_at("123520.25")));
    CHECK(parser.solution().utc_ms == 250);

    REQUIRE(parse(parser, rmc_at("123520")));
    CHECK(parser.solution().utc_ms == 0);
}

// The pinned case: a GGA lost on the wire paired the next RMC with a height 250 ms older.
TEST_CASE("gnss: a GGA and an RMC are one solution only when they name the same instant") {
    NmeaParser parser;
    REQUIRE(parse(parser, gga_at("123519.500")));
    REQUIRE(parse(parser, rmc_at("123519.500")));
    CHECK(parser.same_epoch());

    REQUIRE(parse(parser, rmc_at("123519.750")));
    CHECK_FALSE(parser.same_epoch());

    REQUIRE(parse(parser, gga_at("123520.000")));
    REQUIRE(parse(parser, rmc_at("123520.000")));
    CHECK(parser.same_epoch());
}

// The edge is the top of the second, so a solution three quarters in is dated three quarters on.
TEST_CASE("gnss: a locked PPS edge and the solution's milliseconds date a quarter exactly") {
    GnssSolution solution{};
    solution.pps_latency_ms = 28;
    solution.utc_ms = 500;
    CHECK(solution_instant_ms(solution, 10'580, 10'000, true) == 10'500);

    // The 750 solution can land after the next edge has latched, and it is still the second before.
    solution.utc_ms = 750;
    CHECK(solution_instant_ms(solution, 11'070, 11'000, true) == 10'750);
    CHECK(solution_instant_ms(solution, 10'830, 10'000, true) == 10'750);
}

// The anchored pair names the second an edge opened, so a late solution keeps its own second.
TEST_CASE("gnss: an anchored solution is dated by its own UTC second, however late it lands") {
    GnssSolution solution{};
    solution.utc_valid = true;
    solution.utc_s = 1000;
    CHECK(anchored_instant_ms(solution, 11'070, 10'000, 1000) == 10'000u);

    solution.utc_ms = 250;
    CHECK(anchored_instant_ms(solution, 11'320, 10'000, 1000) == 10'250u);

    // The edge that opened second 1001 was missed, and the anchor still names it.
    solution.utc_s = 1001;
    CHECK(anchored_instant_ms(solution, 11'320, 10'000, 1000) == 11'250u);
}

// An anchor that puts a solution after its arrival is wrong, and one far behind it is stale.
TEST_CASE("gnss: an anchor dates nothing it puts after its arrival, and an old solution old") {
    GnssSolution solution{};
    solution.utc_valid = true;
    solution.utc_s = 1002;
    CHECK_FALSE(anchored_instant_ms(solution, 11'070, 10'000, 1000).has_value());
    solution.utc_s = 990;
    CHECK(anchored_instant_ms(solution, 11'070, 10'000, 1000) == 0u);
    solution.utc_s = 1000;
    CHECK_FALSE(anchored_instant_ms(solution, 11'070, 10'000, 0).has_value());
    solution.utc_valid = false;
    CHECK_FALSE(anchored_instant_ms(solution, 11'070, 10'000, 1000).has_value());
}

// The pinned case: past a missed edge every quarter was dated arrival less latency, 56 ms young.
TEST_CASE("gnss: past a missed edge a solution keeps the phase its stamp names") {
    GnssSolution solution{};
    solution.pps_latency_ms = 28;
    solution.utc_ms = 250;
    CHECK(solution_instant_ms(solution, 11'320, 10'000, true) == 11'250u);
    CHECK(solution_instant_ms(solution, 12'300, 10'000, true) == 12'250u);
}

// The pinned case: the L76K's 31-byte no-time GGA stopped at the length floor, its stamp unread.
TEST_CASE("gnss: a GGA and an RMC that name no time are one pair with no solution") {
    NmeaParser parser;
    REQUIRE(parse(parser, gga_at("123519.750")));
    REQUIRE(parse(parser, rmc_at("123519.750")));
    const char* gga = "$GNGGA,,,,,,0,00,25.5,,,,,,*64";
    const char* rmc = "$GNRMC,,V,,,,,,,,,,N,V*37";
    parser.parse_line(gga, static_cast<int>(strlen(gga)));
    REQUIRE(parser.parse_line(rmc, static_cast<int>(strlen(rmc))));
    CHECK(parser.same_epoch());
    CHECK(parser.stamp_ms() == -1);
    CHECK_FALSE(parser.solution().fix_valid);
}

// A GGA cut short still checksums, and its RMC must not pair with the height it never carried.
TEST_CASE("gnss: a torn GGA that names a time pairs with no RMC") {
    NmeaParser parser;
    const std::string torn = checksummed("GPGGA,123520.000,4807.038,N,0113");
    parser.parse_line(torn.c_str(), static_cast<int>(torn.size()));
    REQUIRE(parse(parser, rmc_at("123520.000")));
    CHECK_FALSE(parser.same_epoch());
}
