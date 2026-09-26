// The only door position gets in through. Everything downstream (the alarm, the
// radar, what we transmit about ourselves) is this parser's output, so a sentence
// that fails its checksum must leave the fix untouched rather than half-applied,
// and a sentence arriving one byte at a time must reconstruct exactly.
#include <cstdlib>  // std::abs
#include <cstring>

#include "core/gnss/nmea.h"
#include "core/units/units.h"
#include "doctest/doctest.h"

using namespace skyblip::gnss;

TEST_CASE("gnss: checksum validation") {
    const char* good = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    CHECK(nmea_checksum_ok(good, static_cast<int>(strlen(good))));
    const char* bad = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*00";
    CHECK_FALSE(nmea_checksum_ok(bad, static_cast<int>(strlen(bad))));
}

TEST_CASE("gnss: coord parse DDMM.mmmm -> 1e-7 deg") {
    int32_t lat = nmea_parse_coord("4807.038", 'N');  // 48 deg 07.038' = 48.1173
    CHECK(std::abs(lat - 481173000) < 20000);
    int32_t lon = nmea_parse_coord("01131.000", 'W');  // -11.51667
    CHECK(lon < 0);
    CHECK(std::abs(lon + 115166667) < 30000);
}

// Found by test/fuzz/fuzz_nmea: thirteen degree digits once overflowed the 1e-7 product.
TEST_CASE("gnss: a coordinate with more degrees than DDDMM holds is no coordinate") {
    CHECK(nmea_parse_coord("480555555555507.4", 'N') == 0);
    CHECK(nmea_parse_coord("4807.038", 'N') != 0);
}

TEST_CASE("gnss: RMC updates fix position, time, speed, track") {
    NmeaParser p;
    const char* rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230825,003.1,W*6B";
    CHECK(p.parse_line(rmc, static_cast<int>(strlen(rmc))));
    CHECK(p.last_sentence() == Sentence::Rmc);
    const GnssSolution& f = p.solution();
    CHECK(f.is_fix);
    CHECK(f.utc_valid);
    CHECK(f.lat_1e7 > 480000000);
    CHECK(f.lon_1e7 > 0);
    // 22.4 knots is 11.524 m/s, and the tenth of a knot the sentence carries survives
    CHECK(f.speed_mm_s == 11524);
    // 84.4 degrees, kept as the receiver reported it rather than as the wire will carry it
    CHECK(f.track_cdeg == 8440);
    // 2025-08-23 12:35:19 UTC epoch
    CHECK(f.utc == 1755952519u);
}

// I, row "Date and jump sanity". An MTK-lineage receiver with no almanac reports
// a date in 1980 beside a position that looks entirely ordinary, and OGN refuses
// it by name (oss/nrf52-ogn-tracker src/ogn.h:737-738). The pinned bug: we took
// any six digits that parsed, so 1980 became an epoch, a flight log session name
// and an ADS-L timestamp, all of them wrong and none of them detectable
// afterwards. The position in the same sentence is not what is being doubted:
// the date is, and it is the date that is dropped.
TEST_CASE("gnss: the MTK year-1980 date is refused, and so is anything before 2000") {
    NmeaParser p;
    const char* lie = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230380,003.1,W*6F";
    REQUIRE(p.parse_line(lie, static_cast<int>(strlen(lie))));
    CHECK(p.solution().is_fix);  // the receiver still claims a solution
    CHECK_FALSE(p.solution().utc_valid);
    CHECK(p.solution().utc == 0);

    // The boundary: 69 is 2069 and believable, 70 is 1970 and a lie. OGN draws
    // the line in the same place.
    CHECK(kMaxTwoDigitYear == 70);

    // A receiver still counting up from nothing sends all zeroes, which is not a
    // date either: month 0 and day 0 do not exist.
    const char* zeroes = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,000000,003.1,W*65";
    REQUIRE(p.parse_line(zeroes, static_cast<int>(strlen(zeroes))));
    CHECK_FALSE(p.solution().utc_valid);

    // And a good date after a bad one clears it: the flag follows the sentence,
    // it is not sticky.
    const char* good = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230825,003.1,W*6B";
    REQUIRE(p.parse_line(good, static_cast<int>(strlen(good))));
    CHECK(p.solution().utc_valid);
}

// Found by test/fuzz/fuzz_nmea on ILP32: '/' is one below '0', so "/0" read as year -10.
TEST_CASE("gnss: a date that is not six digits carries no UTC") {
    NmeaParser p;
    const char* rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,0101/0,003.1,W*7A";
    REQUIRE(p.parse_line(rmc, static_cast<int>(strlen(rmc))));
    CHECK_FALSE(p.solution().utc_valid);
}

// The nRF52's long is 32 bits, and 32 bits of seconds since 1970 run out in January 2038.
TEST_CASE("gnss: a date past 2038 converts where a long is 32 bits") {
    NmeaParser p;
    const char* rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,010140,003.1,W*61";
    REQUIRE(p.parse_line(rmc, static_cast<int>(strlen(rmc))));
    CHECK(p.solution().utc_valid);
    CHECK(p.solution().utc == 2209034119u);
}

// I, row "Leap seconds": DOES NOT APPLY, and this is where it is written down.
// RMC fields 1 and 9 are UTC as the receiver resolved them, so the GPS-UTC
// offset (18 s in 2026) never enters our arithmetic. The moshe-braner fork
// queries it, persists it and reboots when it changes
// (.../src/driver/GNSS.cpp:1610-1679) because it reads u-blox NAV-TIMEGPS, which
// is GPS time. Applying a correction here would move our clock 18 s off UTC.
TEST_CASE("gnss: RMC time is UTC already, so no leap-second offset is applied") {
    NmeaParser p;
    const char* rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230825,003.1,W*6B";
    REQUIRE(p.parse_line(rmc, static_cast<int>(strlen(rmc))));
    // Exactly the UTC epoch of 2025-08-23T12:35:19Z. Not 18 s past it, not 18 s
    // short of it: the same integer any UTC clock would produce.
    CHECK(p.solution().utc == 1755952519u);
    CHECK(p.solution().utc != 1755952519u + 18u);
}

TEST_CASE("gnss: GGA updates altitude and sats") {
    NmeaParser p;
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    CHECK(p.parse_line(gga, static_cast<int>(strlen(gga))));
    const GnssSolution& f = p.solution();
    CHECK(f.alt_msl_mm == 545400);
    CHECK(int(f.sats) == 8);
    CHECK(int(f.fix_quality) == 1);
}

// GGA field 9 is altitude above the geoid (mean sea level); field 11 is the
// separation between the geoid and the WGS-84 ellipsoid at that point. ADS-L 4
// SRD860 issue 2 G.1.7 transmits height above the ELLIPSOID, so the two have to
// be added. Across central Europe the separation is 45-48 m, which is squarely
// inside the vertical alarm window: reporting MSL as if it were HAE puts us that
// far below every correctly implemented neighbour.
TEST_CASE("gnss: GGA carries MSL and ellipsoidal height as separate values") {
    NmeaParser p;
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    REQUIRE(p.parse_line(gga, static_cast<int>(strlen(gga))));
    const GnssSolution& f = p.solution();
    CHECK(f.alt_msl_valid);
    CHECK(f.alt_msl_mm == 545400);
    CHECK(f.geoid_separation_mm == 46900);
    CHECK(f.geoid_separation_measured);
    CHECK(f.alt_hae_valid);
    CHECK(f.alt_mm == 592300);  // 545.4 + 46.9, the value ADS-L wants
}

// Some receivers omit field 11 entirely and some always answer "0.0,M": OGN
// carries a manual override with a 40 m default (oss/nrf52-ogn-tracker
// src/main.h:38, src/gps.cpp:526-529) and SoftRF special-cases the always-zero
// chipsets (oss/SoftRF-lyusupov .../src/driver/GNSS.cpp:1681-1684). We fall back
// to a named regional constant and flag that the separation was assumed, so a
// wrong altitude is attributable rather than silent.
TEST_CASE("gnss: a receiver that omits the geoid separation falls back and says so") {
    NmeaParser p;
    const char* omitted = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,,M,,*52";
    REQUIRE(p.parse_line(omitted, static_cast<int>(strlen(omitted))));
    CHECK_FALSE(p.solution().geoid_separation_measured);
    CHECK(p.solution().geoid_separation_mm == kDefaultGeoidSeparationMm);
    CHECK(p.solution().alt_msl_mm == 545400);
    CHECK(p.solution().alt_mm == 545400 + kDefaultGeoidSeparationMm);
}

TEST_CASE("gnss: a receiver stuck at 0.0 separation falls back too") {
    NmeaParser p;
    const char* zero = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,0.0,M,,*7C";
    REQUIRE(p.parse_line(zero, static_cast<int>(strlen(zero))));
    CHECK_FALSE(p.solution().geoid_separation_measured);
    CHECK(p.solution().geoid_separation_mm == kDefaultGeoidSeparationMm);
    CHECK(p.solution().alt_mm == 545400 + kDefaultGeoidSeparationMm);
}

// GGA field 8. Without it every integrity and accuracy field we transmit is a
// guess, and a guess encoded as zero reads as "no integrity claimed".
TEST_CASE("gnss: GGA carries HDOP in hundredths") {
    NmeaParser p;
    const char* sharp = "$GPGGA,101530,4736.2417,N,00834.9028,E,1,09,1.25,612.3,M,47.4,M,,*70";
    REQUIRE(p.parse_line(sharp, static_cast<int>(strlen(sharp))));
    CHECK(p.solution().hdop_e2 == 125);

    const char* poor = "$GPGGA,101530,4736.2417,N,00834.9028,E,1,09,4.80,612.3,M,47.4,M,,*7A";
    REQUIRE(p.parse_line(poor, static_cast<int>(strlen(poor))));
    CHECK(p.solution().hdop_e2 == 480);
}

// GSA is asked for to carry VDOP: G.1.12's vertical claim has no other source on this part.
TEST_CASE("gnss: GSA carries VDOP in hundredths") {
    NmeaParser p;
    const char* gsa = "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.50,1.25,2.10*0D";
    REQUIRE(p.parse_line(gsa, static_cast<int>(strlen(gsa))));
    CHECK(p.last_sentence() == Sentence::Gsa);
    CHECK(p.solution().vdop_e2 == 210);

    // A 2D solution computes no vertical figure and leaves the field empty.
    const char* flat = "$GPGSA,A,2,04,05,,09,12,,,24,,,,,2.50,1.25,*11";
    REQUIRE(p.parse_line(flat, static_cast<int>(strlen(flat))));
    CHECK(p.solution().vdop_e2 == 0);
}

// Field 2 is the receiver's own answer, where the page used to infer it from the satellite count.
TEST_CASE("gnss: GSA says whether the receiver solved for height") {
    NmeaParser p;
    const char* solid = "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.50,1.25,2.10*0D";
    REQUIRE(p.parse_line(solid, static_cast<int>(strlen(solid))));
    CHECK(p.solution().fix_mode == kFixMode3D);

    const char* flat = "$GPGSA,A,2,04,05,,09,12,,,24,,,,,2.50,1.25,*11";
    REQUIRE(p.parse_line(flat, static_cast<int>(strlen(flat))));
    CHECK(p.solution().fix_mode == kFixMode2D);
}

// One GSA per constellation, and the one whose satellites are in no solution carries no DOP.
TEST_CASE("gnss: a GSA with no DOP at all is a constellation that solved nothing") {
    NmeaParser p;
    const char* solved = "$GNGSA,A,3,04,05,,09,12,,,24,,,,,2.50,1.25,2.10,1*0E";
    REQUIRE(p.parse_line(solved, static_cast<int>(strlen(solved))));
    REQUIRE(p.solution().fix_mode == kFixMode3D);
    REQUIRE(p.solution().vdop_e2 == 210);

    const char* idle = "$GNGSA,A,1,,,,,,,,,,,,,,,,2*1E";
    REQUIRE(p.parse_line(idle, static_cast<int>(strlen(idle))));
    CHECK(p.solution().fix_mode == kFixMode3D);
    CHECK(p.solution().vdop_e2 == 210);
}

// A $PCAS sentence is never acknowledged: what the receiver stops saying is the only evidence.
TEST_CASE("gnss: sentences we switched off are counted, not silently dropped") {
    NmeaParser p;
    const char* gll = "$GPGLL,4736.2417,N,00834.9028,E,101530,A,A*4B";
    const char* vtg = "$GPVTG,084.4,T,,M,022.4,N,041.5,K,A*01";
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";

    CHECK_FALSE(p.parse_line(gll, static_cast<int>(strlen(gll))));
    CHECK_FALSE(p.parse_line(vtg, static_cast<int>(strlen(vtg))));
    CHECK(p.unrequested() == 2);

    REQUIRE(p.parse_line(gga, static_cast<int>(strlen(gga))));
    CHECK(p.unrequested() == 2);

    // A sentence nobody asked about either way is not evidence of anything.
    const char* other = "$GPZDA,101530.00,13,09,2026,00,00*6D";
    CHECK_FALSE(p.parse_line(other, static_cast<int>(strlen(other))));
    CHECK(p.unrequested() == 2);
}

// No fix: no altitude of either kind, and no DOP worth believing.
TEST_CASE("gnss: a GGA without a solution reports no altitude") {
    NmeaParser p;
    const char* none = "$GPGGA,101530,4736.2417,N,00834.9028,E,0,00,99.99,,M,,M,,*7F";
    REQUIRE(p.parse_line(none, static_cast<int>(strlen(none))));
    CHECK_FALSE(p.solution().alt_msl_valid);
    CHECK_FALSE(p.solution().alt_hae_valid);
    CHECK(int(p.solution().fix_quality) == 0);
}

TEST_CASE("gnss: byte-wise feed reconstructs a sentence") {
    NmeaParser p;
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
    bool got = false;
    for (const char* c = gga; *c; c++) got |= p.feed(*c);
    CHECK(got);
    CHECK(p.solution().alt_msl_mm == 545400);
    CHECK(p.solution().alt_mm == 592300);
}

// A burst is always late relative to the PPS edge whose second it describes, so
// the instant a solution was true is earlier than the instant it arrived. The
// receiver knows by how much and stamps it; this is where it comes off again.
// Neither reference trusts the burst without it: SoftRF subtracts a per-chip
// constant, OGN a PPSdelay parameter defaulting to 100 ms.
TEST_CASE("gnss: a fix is timestamped before its sentence arrived") {
    GnssSolution f{};
    f.pps_latency_ms = 135;
    CHECK(solution_instant_ms(f, 10'000) == 9'865);

    // Unstamped, the arrival time is the best we have.
    GnssSolution bare{};
    CHECK(solution_instant_ms(bare, 10'000) == 10'000);

    // Boot: the correction reaches back past zero, and the ages computed from it
    // stay right because the arithmetic wraps the same way on both sides.
    CHECK(static_cast<uint32_t>(500 - solution_instant_ms(f, 100)) == 535);
}

// A latched PPS edge dates the solution exactly, which no per-chip constant can as the burst grows.
TEST_CASE("gnss: a locked PPS edge dates the fix, not the stamped latency") {
    GnssSolution f{};
    f.pps_latency_ms = 333;

    CHECK(solution_instant_ms(f, 10'333, 10'000, true) == 10'000);

    // No lock, or an edge too old to be this burst's own second: the estimate stands.
    CHECK(solution_instant_ms(f, 10'333, 10'000, false) == 10'000);
    CHECK(solution_instant_ms(f, 11'400, 10'000, true) == 11'067);
}

// I, row "Date and jump sanity", second half. A sentence that stopped early
// still carries a checksum over the part that arrived, so the checksum cannot
// catch it: moshe-braner measures the GGA and refuses anything under 40
// characters (.../src/driver/GNSS.cpp:2226-2236). The pinned bug: a GGA cut
// short after the fix quality left the previous altitude standing while the rest
// of the fix moved on, which reads as an aircraft holding altitude perfectly.
TEST_CASE("gnss: a truncated GGA is refused, checksum or no checksum") {
    NmeaParser p;
    const char* whole = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    REQUIRE(p.parse_line(whole, static_cast<int>(strlen(whole))));
    REQUIRE(p.solution().alt_msl_mm == 545400);

    // Cut after HDOP, re-checksummed: eight fields where ten are needed.
    const char* cut = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9*7C";
    REQUIRE(nmea_checksum_ok(cut, static_cast<int>(strlen(cut))));
    CHECK_FALSE(p.parse_line(cut, static_cast<int>(strlen(cut))));

    // Every field empty: the field count is fine and the length is not, which is
    // the case the length rule exists for.
    const char* empty = "$GPGGA,,,,,,,,,,,,,,*56";
    REQUIRE(nmea_checksum_ok(empty, static_cast<int>(strlen(empty))));
    CHECK(static_cast<int>(strlen(empty)) < kMinGgaLength);
    CHECK_FALSE(p.parse_line(empty, static_cast<int>(strlen(empty))));

    // Neither of them touched the solution we already had.
    CHECK(p.solution().alt_msl_mm == 545400);
    CHECK(p.solution().updates == 1);
}

// Found by test/fuzz/fuzz_nmea: twenty digits once overflowed a long on the way to millimetres.
TEST_CASE("gnss: a number too long to be a reading is refused, not wrapped") {
    NmeaParser p;
    const char* gga =
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,99999999999999999999,M,46.9,M,,*69";
    REQUIRE(p.parse_line(gga, static_cast<int>(strlen(gga))));
    CHECK_FALSE(p.solution().alt_msl_valid);

    const char* rmc =
        "$GPRMC,123519,A,4807.038,N,01131.000,E,99999999999999999999,084.4,230394,003.1,W*40";
    REQUIRE(p.parse_line(rmc, static_cast<int>(strlen(rmc))));
    CHECK(p.solution().speed_mm_s == 0);
}

// I, row "Receiver identification". $PCAS06 is answered with a $GPTXT banner and
// nothing else on this receiver answers at all, so the banner is the only proof
// the part in front of us speaks $PCAS. SoftRF reads the version out of the same
// offset (oss/SoftRF-lyusupov .../src/driver/GNSS.cpp:981-1010, 1015-1025).
TEST_CASE("gnss: the receiver names itself in a $GPTXT, and the version is kept") {
    NmeaParser p;
    CHECK_FALSE(p.identified());
    CHECK(p.firmware_version()[0] == 0);

    const char* banner = "$GPTXT,01,01,02,SW=URANUS5,V5.1.0.0*1F";
    CHECK(p.parse_line(banner, static_cast<int>(strlen(banner))));
    CHECK(p.last_sentence() == Sentence::Txt);
    CHECK(p.identified());
    CHECK(strcmp(p.firmware_version(), "URANUS5,V5.1.0.0") == 0);

    // A banner is not a solution: the verification window counts fixes, and a
    // receiver introducing itself must not satisfy it.
    CHECK(p.solution().updates == 0);

    // The part emits other $GPTXT lines (antenna status, start-up notices). A
    // version field that is sometimes an antenna warning is worse than none, so
    // only the SW= banner is taken and the version already learned stands.
    const char* antenna = "$GPTXT,01,01,01,ANTSTATUS=OK*38";
    CHECK_FALSE(p.parse_line(antenna, static_cast<int>(strlen(antenna))));
    CHECK(strcmp(p.firmware_version(), "URANUS5,V5.1.0.0") == 0);
}

TEST_CASE("gnss: corrupt checksum is rejected, no update") {
    NmeaParser p;
    const char* bad = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00";
    CHECK_FALSE(p.parse_line(bad, static_cast<int>(strlen(bad))));
    CHECK(p.solution().updates == 0);
}

// GSV is the only sentence carrying a satellite's signal level, and the talker its constellation.
TEST_CASE("gnss: GSV fills the sky view, four satellites to a sentence") {
    NmeaParser p;
    const char* first = "$GPGSV,2,1,05,01,40,083,42,02,30,120,38,03,60,200,40,04,20,300,35,0*62";
    const char* second = "$GPGSV,2,2,05,05,10,010,,0*55";
    REQUIRE(p.parse_line(first, static_cast<int>(strlen(first))));
    REQUIRE(p.parse_line(second, static_cast<int>(strlen(second))));

    const SkyView& sky = p.sky();
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
    NmeaParser p;
    const char* set = "$GPGSV,1,1,02,01,40,083,42,02,30,120,38,0*66";
    REQUIRE(p.parse_line(set, static_cast<int>(strlen(set))));
    REQUIRE(p.parse_line(set, static_cast<int>(strlen(set))));
    CHECK(p.sky().count() == 2);

    // Another constellation is another set: the two stand side by side.
    const char* beidou = "$BDGSV,1,1,01,07,50,150,44,0*43";
    REQUIRE(p.parse_line(beidou, static_cast<int>(strlen(beidou))));
    CHECK(p.sky().count() == 3);
    CHECK(p.sky().in_view_of(System::Gps) == 2);
    CHECK(p.sky().in_view_of(System::Beidou) == 1);
    CHECK(p.sky().at(2).system == System::Beidou);
}

// GSA lists what solved, GSV what is up there: the page draws the difference.
TEST_CASE("gnss: the satellites GSA named are the ones GSV marks as in the solution") {
    NmeaParser p;
    const char* gsa = "$GNGSA,A,3,01,03,,,,,,,,,,,2.50,1.25,2.10,1*01";
    const char* gsv = "$GPGSV,1,1,03,01,40,083,42,02,30,120,38,03,60,200,40,0*54";
    REQUIRE(p.parse_line(gsa, static_cast<int>(strlen(gsa))));
    REQUIRE(p.parse_line(gsv, static_cast<int>(strlen(gsv))));

    const SkyView& sky = p.sky();
    REQUIRE(sky.count() == 3);
    CHECK(sky.at(0).in_use);
    CHECK_FALSE(sky.at(1).in_use);
    CHECK(sky.at(2).in_use);
    CHECK(sky.in_use() == 2);
    CHECK(sky.in_use_of(System::Gps) == 2);
}

// QZSS answers on the GP talker, so the id is the only thing that tells it from a GPS satellite.
TEST_CASE("gnss: a QZSS satellite on the GP talker is not counted as GPS") {
    NmeaParser p;
    const char* gsv = "$GPGSV,1,1,02,01,40,083,42,193,70,140,45,0*57";
    REQUIRE(p.parse_line(gsv, static_cast<int>(strlen(gsv))));
    CHECK(p.sky().in_view_of(System::Gps) == 1);
    CHECK(p.sky().in_view_of(System::Qzss) == 1);
}

// The solution set is the burst's, so a satellite that drops out of it stops being marked.
TEST_CASE("gnss: each burst's GGA starts the solution set again") {
    NmeaParser p;
    const char* gsa = "$GNGSA,A,3,01,03,,,,,,,,,,,2.50,1.25,2.10,1*01";
    const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    REQUIRE(p.parse_line(gsa, static_cast<int>(strlen(gsa))));
    REQUIRE(p.sky().in_use() == 2);

    REQUIRE(p.parse_line(gga, static_cast<int>(strlen(gga))));
    CHECK(p.sky().in_use() == 0);
}

// The pinned bug: a solution quantised to the wire's units at the door, so a turn rate
// differentiated from it could only ever be a whole degree a second.
TEST_CASE("gnss: a solution is kept at the precision the receiver reported, not the wire's") {
    NmeaParser p;
    const char* rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.47,084.43,230825,,,A*79";
    REQUIRE(p.parse_line(rmc, static_cast<int>(strlen(rmc))));
    const GnssSolution& f = p.solution();

    // 22.47 kt is 11.559 m/s, which cordic9 and quarter-m/s would both round away.
    CHECK(f.speed_mm_s == 11560);
    CHECK(f.track_cdeg == 8443);

    // And the wire's own units are still one conversion away, rounded.
    CHECK(skyblip::to_speed_q(skyblip::MillimetresPerSec(f.speed_mm_s)).v == 46);
    CHECK(skyblip::to_cordic9(skyblip::CentiDegrees(f.track_cdeg)).v == 120);
}
