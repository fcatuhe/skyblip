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
