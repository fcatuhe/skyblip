// The only door position gets in through. Everything downstream (the alarm, the
// radar, what we transmit about ourselves) is this parser's output, so a sentence
// that fails its checksum must leave the fix untouched rather than half-applied,
// and a sentence arriving one byte at a time must reconstruct exactly.
#include <cstdio>
#include <cstdlib>  // std::abs
#include <cstring>
#include <string>

#include "core/gnss/nmea.h"
#include "core/units/units.h"
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
}  // namespace

TEST_CASE("gnss: checksum validation") {
    const char* good = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    CHECK(nmea_checksum_ok(good, static_cast<int>(strlen(good))));
    const char* bad = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*00";
    CHECK_FALSE(nmea_checksum_ok(bad, static_cast<int>(strlen(bad))));
}

TEST_CASE("gnss: a DDMM.mmmm coordinate parses to 1e-7 degrees") {
    int32_t latitude = 0;
    REQUIRE(nmea_parse_coordinate("4807.038", 'N', latitude));  // 48 deg 07.038' = 48.1173
    CHECK(std::abs(latitude - 481173000) < 20000);
    int32_t longitude = 0;
    REQUIRE(nmea_parse_coordinate("01131.000", 'W', longitude));  // -11.51667
    CHECK(longitude < 0);
    CHECK(std::abs(longitude + 115166667) < 30000);
}

TEST_CASE("gnss: RMC updates fix position, time, speed, track") {
    NmeaParser parser;
    const char* rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230825,003.1,W*6B";
    CHECK(parser.parse_line(rmc, static_cast<int>(strlen(rmc))));
    CHECK(parser.last_sentence() == Sentence::Rmc);
    const GnssSolution& solution = parser.solution();
    CHECK(solution.fix_valid);
    CHECK(solution.utc_valid);
    CHECK(solution.latitude_1e7 > 480000000);
    CHECK(solution.longitude_1e7 > 0);
    // 22.4 knots is 11.524 m/s, and the tenth of a knot the sentence carries survives
    CHECK(solution.speed_mm_s == 11524);
    // 84.4 degrees, kept as the receiver reported it rather than as the wire will carry it
    CHECK(solution.track_cdeg == 8440);
    // 2025-08-23 12:35:19 UTC epoch
    CHECK(solution.utc_s == 1755952519u);
}

TEST_CASE("gnss: an RMC that claims a fix with no position is not a fix, whatever came before") {
    NmeaParser parser;
    REQUIRE(
        parse(parser, checksummed("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230825,,")));
    REQUIRE(parser.solution().fix_valid);

    REQUIRE(parse(parser, checksummed("GPRMC,123520,A,,,,,022.4,084.4,230825,,")));
    CHECK_FALSE(parser.solution().fix_valid);

    REQUIRE(parse(parser, checksummed("GPRMC,123521,A,4807.038,N,,,022.4,084.4,230825,,")));
    CHECK_FALSE(parser.solution().fix_valid);
}

TEST_CASE("gnss: a latitude past 90, a longitude past 180 or a 60th minute is not a fix") {
    for (const char* body : {"GPRMC,123519,A,9000.001,N,01131.000,E,022.4,084.4,230825,,",
                             "GPRMC,123519,A,4807.038,N,18000.001,W,022.4,084.4,230825,,",
                             "GPRMC,123519,A,4860.000,N,01131.000,E,022.4,084.4,230825,,",
                             "GPRMC,123519,A,99999999999.000,N,01131.000,E,022.4,084.4,230825,,",
                             "GPRMC,123519,A,4807.038,,01131.000,E,022.4,084.4,230825,,"}) {
        CAPTURE(body);
        NmeaParser parser;
        REQUIRE(parse(parser, checksummed(body)));
        CHECK_FALSE(parser.solution().fix_valid);
    }

    NmeaParser parser;
    REQUIRE(
        parse(parser, checksummed("GPRMC,123519,A,9000.000,S,18000.000,W,022.4,084.4,230825,,")));
    CHECK(parser.solution().fix_valid);
    CHECK(parser.solution().latitude_1e7 == -900000000);
    CHECK(parser.solution().longitude_1e7 == -1800000000);
}

TEST_CASE("gnss: a speed or an altitude no civil receiver reports is refused, never overflowed") {
    NmeaParser parser;
    REQUIRE(
        parse(parser, checksummed("GPRMC,123519,A,4807.038,N,01131.000,E,1000.1,084.4,230825,,")));
    CHECK(parser.solution().speed_mm_s == 0);
    REQUIRE(
        parse(parser, checksummed("GPRMC,123519,A,4807.038,N,01131.000,E,99999999999999999999.9,"
                                  "084.4,230825,,")));
    CHECK(parser.solution().speed_mm_s == 0);

    for (const char* altitude : {"100000.1", "-1000.1", "99999999999999999999.9"}) {
        CAPTURE(altitude);
        const std::string body =
            std::string("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,") + altitude + ",M,46.9,M,,";
        REQUIRE(parse(parser, checksummed(body.c_str())));
        CHECK_FALSE(parser.solution().altitude_msl_valid);
    }
}

// A refused speed kept the one before it, and the case beside it only ever fed a fresh parser.
TEST_CASE("gnss: a refused speed after a good one is not the good one carried over") {
    for (const char* speed : {"1000.1", "99999999999999999999.9", ""}) {
        CAPTURE(speed);
        NmeaParser parser;
        REQUIRE(parse(parser,
                      checksummed("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230825,,")));
        REQUIRE(parser.solution().speed_mm_s == 11524);
        const std::string body =
            std::string("GPRMC,123520,A,4807.038,N,01131.000,E,") + speed + ",084.4,230825,,";
        REQUIRE(parse(parser, checksummed(body.c_str())));
        CHECK(parser.solution().speed_mm_s == 0);
    }
}

TEST_CASE("gnss: a date past January 2038 is still the right UTC second") {
    NmeaParser parser;
    REQUIRE(
        parse(parser, checksummed("GPRMC,000000,A,4807.038,N,01131.000,E,022.4,084.4,010140,,")));
    CHECK(parser.solution().utc_valid);
    CHECK(parser.solution().utc_s == 2208988800u);
}

// I, row "Date and jump sanity". An MTK-lineage receiver with no almanac reports
// a date in 1980 beside a position that looks entirely ordinary, and OGN refuses
// it by name (oss/nrf52-ogn-tracker src/ogn.h:737-738). The pinned bug: we took
// any six digits that parsed, so 1980 became an epoch, a flight log session name
// and an ADS-L timestamp, all of them wrong and none of them detectable
// afterwards. The position in the same sentence is not what is being doubted:
// the date is, and it is the date that is dropped.
TEST_CASE("gnss: the MTK year-1980 date is refused, and so is anything before 2000") {
    NmeaParser parser;
    const char* lie = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230380,003.1,W*6F";
    REQUIRE(parser.parse_line(lie, static_cast<int>(strlen(lie))));
    CHECK(parser.solution().fix_valid);  // the receiver still claims a solution
    CHECK_FALSE(parser.solution().utc_valid);
    CHECK(parser.solution().utc_s == 0);

    // The boundary: 69 is 2069 and believable, 70 is 1970 and a lie. OGN draws
    // the line in the same place.
    CHECK(kTwoDigitYearCeiling == 70);

    // A receiver still counting up from nothing sends all zeroes, which is not a
    // date either: month 0 and day 0 do not exist.
    const char* zeroes = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,000000,003.1,W*65";
    REQUIRE(parser.parse_line(zeroes, static_cast<int>(strlen(zeroes))));
    CHECK_FALSE(parser.solution().utc_valid);

    // And a good date after a bad one clears it: the flag follows the sentence,
    // it is not sticky.
    const char* good = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230825,003.1,W*6B";
    REQUIRE(parser.parse_line(good, static_cast<int>(strlen(good))));
    CHECK(parser.solution().utc_valid);
}

// Found by test/fuzz/fuzz_nmea on ILP32: '/' is one below '0', so "/0" read as year -10.
TEST_CASE("gnss: a date that is not six digits carries no UTC") {
    NmeaParser parser;
    const char* rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,0101/0,003.1,W*7A";
    REQUIRE(parser.parse_line(rmc, static_cast<int>(strlen(rmc))));
    CHECK_FALSE(parser.solution().utc_valid);
}

// I, row "Leap seconds": DOES NOT APPLY, and this is where it is written down.
// RMC fields 1 and 9 are UTC as the receiver resolved them, so the GPS-UTC
// offset (18 s in 2026) never enters our arithmetic. The moshe-braner fork
// queries it, persists it and reboots when it changes
// (.../src/driver/GNSS.cpp:1610-1679) because it reads u-blox NAV-TIMEGPS, which
// is GPS time. Applying a correction here would move our clock 18 s off UTC.
TEST_CASE("gnss: RMC time is UTC already, so no leap-second offset is applied") {
    NmeaParser parser;
    const char* rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230825,003.1,W*6B";
    REQUIRE(parser.parse_line(rmc, static_cast<int>(strlen(rmc))));
    // Exactly the UTC epoch of 2025-08-23T12:35:19Z. Not 18 s past it, not 18 s
    // short of it: the same integer any UTC clock would produce.
    CHECK(parser.solution().utc_s == 1755952519u);
    CHECK(parser.solution().utc_s != 1755952519u + 18u);
}

TEST_CASE("gnss: GGA updates altitude and sats") {
    NmeaParser parser;
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    CHECK(parser.parse_line(gga, static_cast<int>(strlen(gga))));
    const GnssSolution& solution = parser.solution();
    CHECK(solution.altitude_msl_mm == 545400);
    CHECK(int(solution.satellites) == 8);
    CHECK(int(solution.fix_quality) == 1);
}

// GGA field 9 is altitude above the geoid (mean sea level); field 11 is the
// separation between the geoid and the WGS-84 ellipsoid at that point. ADS-L 4
// SRD860 issue 2 G.1.7 transmits height above the ELLIPSOID, so the two have to
// be added. Across central Europe the separation is 45-48 m, which is squarely
// inside the vertical alarm window: reporting MSL as if it were HAE puts us that
// far below every correctly implemented neighbour.
TEST_CASE("gnss: GGA carries MSL and ellipsoidal height as separate values") {
    NmeaParser parser;
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    REQUIRE(parser.parse_line(gga, static_cast<int>(strlen(gga))));
    const GnssSolution& solution = parser.solution();
    CHECK(solution.altitude_msl_valid);
    CHECK(solution.altitude_msl_mm == 545400);
    CHECK(solution.geoid_separation_mm == 46900);
    CHECK(solution.geoid_measured);
    CHECK(solution.altitude_hae_valid);
    CHECK(solution.altitude_hae_mm == 592300);  // 545.4 + 46.9, the value ADS-L wants
}

// Some receivers omit field 11 entirely and some always answer "0.0,M": OGN
// carries a manual override with a 40 m default (oss/nrf52-ogn-tracker
// src/main.h:38, src/gps.cpp:526-529) and SoftRF special-cases the always-zero
// chipsets (oss/SoftRF-lyusupov .../src/driver/GNSS.cpp:1681-1684). We fall back
// to a named regional constant and flag that the separation was assumed, so a
// wrong altitude is attributable rather than silent.
TEST_CASE("gnss: a receiver that omits the geoid separation falls back and says so") {
    NmeaParser parser;
    const char* omitted = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,,M,,*52";
    REQUIRE(parser.parse_line(omitted, static_cast<int>(strlen(omitted))));
    CHECK_FALSE(parser.solution().geoid_measured);
    CHECK(parser.solution().geoid_separation_mm == kDefaultGeoidSeparationMm);
    CHECK(parser.solution().altitude_msl_mm == 545400);
    CHECK(parser.solution().altitude_hae_mm == 545400 + kDefaultGeoidSeparationMm);
}

TEST_CASE("gnss: a receiver stuck at 0.0 separation falls back too") {
    NmeaParser parser;
    const char* zero = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,0.0,M,,*7C";
    REQUIRE(parser.parse_line(zero, static_cast<int>(strlen(zero))));
    CHECK_FALSE(parser.solution().geoid_measured);
    CHECK(parser.solution().geoid_separation_mm == kDefaultGeoidSeparationMm);
    CHECK(parser.solution().altitude_hae_mm == 545400 + kDefaultGeoidSeparationMm);
}

// GGA field 8. Without it every integrity and accuracy field we transmit is a
// guess, and a guess encoded as zero reads as "no integrity claimed".
TEST_CASE("gnss: GGA carries HDOP in hundredths") {
    NmeaParser parser;
    const char* sharp = "$GPGGA,101530,4736.2417,N,00834.9028,E,1,09,1.25,612.3,M,47.4,M,,*70";
    REQUIRE(parser.parse_line(sharp, static_cast<int>(strlen(sharp))));
    CHECK(parser.solution().hdop_e2 == 125);

    const char* poor = "$GPGGA,101530,4736.2417,N,00834.9028,E,1,09,4.80,612.3,M,47.4,M,,*7A";
    REQUIRE(parser.parse_line(poor, static_cast<int>(strlen(poor))));
    CHECK(parser.solution().hdop_e2 == 480);
}

// GSA is asked for to carry VDOP: G.1.15's vertical claim has no other source on this part.
TEST_CASE("gnss: GSA carries VDOP in hundredths") {
    NmeaParser parser;
    const char* gsa = "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.50,1.25,2.10*0D";
    REQUIRE(parser.parse_line(gsa, static_cast<int>(strlen(gsa))));
    CHECK(parser.last_sentence() == Sentence::Gsa);
    CHECK(parser.solution().vdop_e2 == 210);

    // A 2D solution computes no vertical figure and leaves the field empty.
    const char* flat = "$GPGSA,A,2,04,05,,09,12,,,24,,,,,2.50,1.25,*11";
    REQUIRE(parser.parse_line(flat, static_cast<int>(strlen(flat))));
    CHECK(parser.solution().vdop_e2 == 0);
}

// Field 2 is the receiver's own answer, where the page used to infer it from the satellite count.
TEST_CASE("gnss: GSA says whether the receiver solved for height") {
    NmeaParser parser;
    const char* solid = "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.50,1.25,2.10*0D";
    REQUIRE(parser.parse_line(solid, static_cast<int>(strlen(solid))));
    CHECK(parser.solution().fix_mode == kFixMode3D);

    const char* flat = "$GPGSA,A,2,04,05,,09,12,,,24,,,,,2.50,1.25,*11";
    REQUIRE(parser.parse_line(flat, static_cast<int>(strlen(flat))));
    CHECK(parser.solution().fix_mode == kFixMode2D);
}

// One GSA per constellation, and the one whose satellites are in no solution carries no DOP.
TEST_CASE("gnss: a GSA with no DOP at all is a constellation that solved nothing") {
    NmeaParser parser;
    const char* solved = "$GNGSA,A,3,04,05,,09,12,,,24,,,,,2.50,1.25,2.10,1*0E";
    REQUIRE(parser.parse_line(solved, static_cast<int>(strlen(solved))));
    REQUIRE(parser.solution().fix_mode == kFixMode3D);
    REQUIRE(parser.solution().vdop_e2 == 210);

    const char* idle = "$GNGSA,A,1,,,,,,,,,,,,,,,,2*1E";
    REQUIRE(parser.parse_line(idle, static_cast<int>(strlen(idle))));
    CHECK(parser.solution().fix_mode == kFixMode3D);
    CHECK(parser.solution().vdop_e2 == 210);
}

// A $PCAS sentence is never acknowledged: what the receiver stops saying is the only evidence.
TEST_CASE("gnss: sentences we switched off are counted, not silently dropped") {
    NmeaParser parser;
    const char* gll = "$GPGLL,4736.2417,N,00834.9028,E,101530,A,A*4B";
    const char* vtg = "$GPVTG,084.4,T,,M,022.4,N,041.5,K,A*01";
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";

    CHECK_FALSE(parser.parse_line(gll, static_cast<int>(strlen(gll))));
    CHECK_FALSE(parser.parse_line(vtg, static_cast<int>(strlen(vtg))));
    CHECK(parser.unrequested() == 2);

    REQUIRE(parser.parse_line(gga, static_cast<int>(strlen(gga))));
    CHECK(parser.unrequested() == 2);

    // A sentence nobody asked about either way is not evidence of anything.
    const char* other = "$GPZDA,101530.00,13,09,2026,00,00*6D";
    CHECK_FALSE(parser.parse_line(other, static_cast<int>(strlen(other))));
    CHECK(parser.unrequested() == 2);
}

// No fix: no altitude of either kind, and no DOP worth believing.
TEST_CASE("gnss: a GGA without a solution reports no altitude") {
    NmeaParser parser;
    const char* none = "$GPGGA,101530,4736.2417,N,00834.9028,E,0,00,99.99,,M,,M,,*7F";
    REQUIRE(parser.parse_line(none, static_cast<int>(strlen(none))));
    CHECK_FALSE(parser.solution().altitude_msl_valid);
    CHECK_FALSE(parser.solution().altitude_hae_valid);
    CHECK(int(parser.solution().fix_quality) == 0);
}

TEST_CASE("gnss: byte-wise feed reconstructs a sentence") {
    NmeaParser parser;
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
    bool got = false;
    for (const char* character = gga; *character; character++) got |= parser.feed(*character);
    CHECK(got);
    CHECK(parser.solution().altitude_msl_mm == 545400);
    CHECK(parser.solution().altitude_hae_mm == 592300);
}

// A burst is always late relative to the PPS edge whose second it describes, so
// the instant a solution was true is earlier than the instant it arrived. The
// receiver knows by how much and stamps it; this is where it comes off again.
// Neither reference trusts the burst without it: SoftRF subtracts a per-chip
// constant, OGN a PPSdelay parameter defaulting to 100 ms.
TEST_CASE("gnss: a fix is timestamped before its sentence arrived") {
    GnssSolution solution{};
    solution.pps_latency_ms = 135;
    CHECK(solution_instant_ms(solution, 10'000) == 9'865);

    // Unstamped, the arrival time is the best we have.
    GnssSolution bare{};
    CHECK(solution_instant_ms(bare, 10'000) == 10'000);

    // Boot: the correction reaches back past zero, and the ages computed from it
    // stay right because the arithmetic wraps the same way on both sides.
    CHECK(static_cast<uint32_t>(500 - solution_instant_ms(solution, 100)) == 535);
}

// A latched PPS edge dates the solution exactly, which no per-chip constant can as the burst grows.
TEST_CASE("gnss: a locked PPS edge dates the fix, not the stamped latency") {
    GnssSolution solution{};
    solution.pps_latency_ms = 333;

    CHECK(solution_instant_ms(solution, 10'333, 10'000, true) == 10'000);

    // No lock, or an edge too old to be this burst's own second: the estimate stands.
    CHECK(solution_instant_ms(solution, 10'333, 10'000, false) == 10'000);
    CHECK(solution_instant_ms(solution, 11'400, 10'000, true) == 11'067);
}

// I, row "Date and jump sanity", second half. A sentence that stopped early
// still carries a checksum over the part that arrived, so the checksum cannot
// catch it: moshe-braner measures the GGA and refuses anything under 40
// characters (.../src/driver/GNSS.cpp:2226-2236). The pinned bug: a GGA cut
// short after the fix quality left the previous altitude standing while the rest
// of the fix moved on, which reads as an aircraft holding altitude perfectly.
TEST_CASE("gnss: a truncated GGA is refused, checksum or no checksum") {
    NmeaParser parser;
    const char* whole = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    REQUIRE(parser.parse_line(whole, static_cast<int>(strlen(whole))));
    REQUIRE(parser.solution().altitude_msl_mm == 545400);

    // Cut after HDOP, re-checksummed: eight fields where ten are needed.
    const char* cut = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9*7C";
    REQUIRE(nmea_checksum_ok(cut, static_cast<int>(strlen(cut))));
    CHECK_FALSE(parser.parse_line(cut, static_cast<int>(strlen(cut))));

    // Every field empty: the field count is fine and the length is not, which is
    // the case the length rule exists for.
    const char* empty = "$GPGGA,,,,,,,,,,,,,,*56";
    REQUIRE(nmea_checksum_ok(empty, static_cast<int>(strlen(empty))));
    CHECK(static_cast<int>(strlen(empty)) < kGgaLengthFloorBytes);
    CHECK_FALSE(parser.parse_line(empty, static_cast<int>(strlen(empty))));

    // Neither of them touched the solution we already had.
    CHECK(parser.solution().altitude_msl_mm == 545400);
    CHECK(parser.solution().sentences == 1);
}

// I, row "Receiver identification". $PCAS06 is answered with a $GPTXT banner and
// nothing else on this receiver answers at all, so the banner is the only proof
// the part in front of us speaks $PCAS. SoftRF reads the version out of the same
// offset (oss/SoftRF-lyusupov .../src/driver/GNSS.cpp:981-1010, 1015-1025).
TEST_CASE("gnss: the receiver names itself in a $GPTXT, and the version is kept") {
    NmeaParser parser;
    CHECK_FALSE(parser.identified());
    CHECK(parser.firmware_version()[0] == 0);

    const char* banner = "$GPTXT,01,01,02,SW=URANUS5,V5.1.0.0*1F";
    CHECK(parser.parse_line(banner, static_cast<int>(strlen(banner))));
    CHECK(parser.last_sentence() == Sentence::Txt);
    CHECK(parser.identified());
    CHECK(strcmp(parser.firmware_version(), "URANUS5,V5.1.0.0") == 0);

    // A banner is not a solution: the verification window counts fixes, and a
    // receiver introducing itself must not satisfy it.
    CHECK(parser.solution().sentences == 0);

    // The part emits other $GPTXT lines (antenna status, start-up notices). A
    // version field that is sometimes an antenna warning is worse than none, so
    // only the SW= banner is taken and the version already learned stands.
    const char* antenna = "$GPTXT,01,01,01,ANTSTATUS=OK*38";
    CHECK_FALSE(parser.parse_line(antenna, static_cast<int>(strlen(antenna))));
    CHECK(strcmp(parser.firmware_version(), "URANUS5,V5.1.0.0") == 0);
}

TEST_CASE("gnss: corrupt checksum is rejected, no update") {
    NmeaParser parser;
    const char* bad = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00";
    CHECK_FALSE(parser.parse_line(bad, static_cast<int>(strlen(bad))));
    CHECK(parser.solution().sentences == 0);
}

// The pinned bug: a solution quantised to the wire's units at the door, so a turn rate
// differentiated from it could only ever be a whole degree a second.
TEST_CASE("gnss: a solution is kept at the precision the receiver reported, not the wire's") {
    NmeaParser parser;
    const char* rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.47,084.43,230825,,,A*79";
    REQUIRE(parser.parse_line(rmc, static_cast<int>(strlen(rmc))));
    const GnssSolution& solution = parser.solution();

    // 22.47 kt is 11.559 m/s, which cordic9 and quarter-m/s would both round away.
    CHECK(solution.speed_mm_s == 11560);
    CHECK(solution.track_cdeg == 8443);

    // And the wire's own units are still one conversion away, rounded.
    CHECK(skyblip::to_speed_q(skyblip::MillimetresPerSecond(solution.speed_mm_s)).value == 46);
    CHECK(skyblip::to_cordic9(skyblip::CentiDegrees(solution.track_cdeg)).value == 120);
}
