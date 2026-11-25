// RMC's speed and course: what the receiver reported, and no value where it reported none.
#include <string>

#include "core/gnss/nmea.h"
#include "doctest/doctest.h"
#include "test/support/nmea_checksum.h"

using namespace skyblip::gnss;
using skyblip::checksummed;

namespace {
bool parse(NmeaParser& parser, const std::string& line) {
    return parser.parse_line(line.c_str(), static_cast<int>(line.size()));
}
}  // namespace

// The L76K leaves SOG and COG empty "in case of an invalid value", and both used to read as 0.
TEST_CASE("gnss: an RMC with an empty speed or course carries none, and 0.00 kn is a speed") {
    NmeaParser parser;
    REQUIRE(parse(parser, checksummed("GNRMC,123519.000,A,4807.0380,N,01131.0000,E,,,230825,,,A")));
    CHECK(parser.solution().fix_valid);
    CHECK_FALSE(parser.solution().speed_valid);
    CHECK_FALSE(parser.solution().track_valid);

    REQUIRE(
        parse(parser, checksummed("GNRMC,123520.000,A,4807.0380,N,01131.0000,E,0.00,,230825,,,A")));
    CHECK(parser.solution().speed_valid);
    CHECK(parser.solution().speed_mm_s == 0);
    CHECK_FALSE(parser.solution().track_valid);

    REQUIRE(parse(
        parser, checksummed("GNRMC,123521.000,A,4807.0380,N,01131.0000,E,0.00,123.40,230825,,,A")));
    CHECK(parser.solution().speed_valid);
    CHECK(parser.solution().track_valid);
    CHECK(parser.solution().track_cdeg == 12340);

    REQUIRE(parse(parser, checksummed("GNRMC,123522.000,V,,,,,,,230825,,,N")));
    CHECK_FALSE(parser.solution().speed_valid);
    CHECK_FALSE(parser.solution().track_valid);
}

// G.1.8 saturates a measured speed, so a fast one is held at COCOM's 1000 kn rather than dropped.
TEST_CASE("gnss: a speed past 1000 kn is held there, and one past any integer is none") {
    NmeaParser parser;
    REQUIRE(
        parse(parser, checksummed("GPRMC,123519,A,4807.038,N,01131.000,E,1001.00,084.4,230825,,")));
    CHECK(parser.solution().speed_valid);
    CHECK(parser.solution().speed_mm_s == 514444);
    REQUIRE(
        parse(parser, checksummed("GPRMC,123519,A,4807.038,N,01131.000,E,99999999999999999999.9,"
                                  "084.4,230825,,")));
    CHECK_FALSE(parser.solution().speed_valid);
    CHECK(parser.solution().speed_mm_s == 0);
}

// A refused speed kept the one before it, and the case beside it only ever fed a fresh parser.
TEST_CASE("gnss: a refused speed after a good one is not the good one carried over") {
    for (const char* speed : {"-1.0", "99999999999999999999.9", ""}) {
        CAPTURE(speed);
        NmeaParser parser;
        REQUIRE(parse(parser,
                      checksummed("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230825,,")));
        REQUIRE(parser.solution().speed_mm_s == 11524);
        const std::string body =
            std::string("GPRMC,123520,A,4807.038,N,01131.000,E,") + speed + ",084.4,230825,,";
        REQUIRE(parse(parser, checksummed(body.c_str())));
        CHECK_FALSE(parser.solution().speed_valid);
        CHECK(parser.solution().speed_mm_s == 0);
    }
}
