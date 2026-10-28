// The sky view a GSV set fills, and which of its satellites the solution used.
#include <cstring>

#include "core/gnss/nmea.h"
#include "doctest/doctest.h"

using namespace skyblip::gnss;

// GSV is the only sentence carrying a satellite's signal level, and the talker its constellation.
TEST_CASE("gnss: GSV fills the sky view, four satellites to a sentence") {
    NmeaParser parser;
    const char* first = "$GPGSV,2,1,05,01,40,083,42,02,30,120,38,03,60,200,40,04,20,300,35,0*62";
    const char* second = "$GPGSV,2,2,05,05,10,010,,0*55";
    REQUIRE(parser.parse_line(first, static_cast<int>(strlen(first))));
    REQUIRE(parser.parse_line(second, static_cast<int>(strlen(second))));

    const SkyView& sky = parser.sky();
    CHECK(sky.count() == 5);
    CHECK(sky.in_view_of(System::Gps) == 5);
    CHECK(sky.at(0).id == 1);
    CHECK(sky.at(0).elevation_deg == 40);
    CHECK(sky.at(0).azimuth_deg == 83);
    CHECK(sky.at(0).cn0_dbhz == 42);
    // In view and not tracked: the level field is empty, which is not a level of zero dB-Hz.
    CHECK(sky.at(4).id == 5);
    CHECK(sky.at(4).cn0_dbhz == 0);
}

TEST_CASE("gnss: a second GSV set replaces the first, it does not pile up") {
    NmeaParser parser;
    const char* set = "$GPGSV,1,1,02,01,40,083,42,02,30,120,38,0*66";
    REQUIRE(parser.parse_line(set, static_cast<int>(strlen(set))));
    REQUIRE(parser.parse_line(set, static_cast<int>(strlen(set))));
    CHECK(parser.sky().count() == 2);

    // Another constellation is another set: the two stand side by side.
    const char* beidou = "$BDGSV,1,1,01,07,50,150,44,0*43";
    REQUIRE(parser.parse_line(beidou, static_cast<int>(strlen(beidou))));
    CHECK(parser.sky().count() == 3);
    CHECK(parser.sky().in_view_of(System::Gps) == 2);
    CHECK(parser.sky().in_view_of(System::Beidou) == 1);
    CHECK(parser.sky().at(2).system == System::Beidou);
}

// GSA lists what solved, GSV what is up there: the page draws the difference.
TEST_CASE("gnss: the satellites GSA named are the ones GSV marks as in the solution") {
    NmeaParser parser;
    const char* gsa = "$GNGSA,A,3,01,03,,,,,,,,,,,2.50,1.25,2.10,1*01";
    const char* gsv = "$GPGSV,1,1,03,01,40,083,42,02,30,120,38,03,60,200,40,0*54";
    REQUIRE(parser.parse_line(gsa, static_cast<int>(strlen(gsa))));
    REQUIRE(parser.parse_line(gsv, static_cast<int>(strlen(gsv))));

    const SkyView& sky = parser.sky();
    REQUIRE(sky.count() == 3);
    CHECK(sky.at(0).in_use);
    CHECK_FALSE(sky.at(1).in_use);
    CHECK(sky.at(2).in_use);
    CHECK(sky.in_use() == 2);
    CHECK(sky.in_use_of(System::Gps) == 2);
}

// QZSS answers on the GP talker, so the id is the only thing that tells it from a GPS satellite.
TEST_CASE("gnss: a QZSS satellite on the GP talker is not counted as GPS") {
    NmeaParser parser;
    const char* gsv = "$GPGSV,1,1,02,01,40,083,42,193,70,140,45,0*57";
    REQUIRE(parser.parse_line(gsv, static_cast<int>(strlen(gsv))));
    CHECK(parser.sky().in_view_of(System::Gps) == 1);
    CHECK(parser.sky().in_view_of(System::Qzss) == 1);
}

// The solution set is the burst's, so a satellite that drops out of it stops being marked.
TEST_CASE("gnss: each burst's GGA starts the solution set again") {
    NmeaParser parser;
    const char* gsa = "$GNGSA,A,3,01,03,,,,,,,,,,,2.50,1.25,2.10,1*01";
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    REQUIRE(parser.parse_line(gsa, static_cast<int>(strlen(gsa))));
    REQUIRE(parser.sky().in_use() == 2);

    REQUIRE(parser.parse_line(gga, static_cast<int>(strlen(gga))));
    CHECK(parser.sky().in_use() == 0);
}
