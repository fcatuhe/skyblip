// What an EFB is told, in the FLARM sentences it already speaks. The geometry is
// the part with teeth: PFLAA is relative to own-ship, so a sign error puts traffic
// on the wrong side of the aircraft on a moving map. Without an own position there
// is no relative anything, and the encoder must emit nothing rather than a target
// relative to (0, 0).
#include <algorithm>  // std::count
#include <cstdlib>    // std::abs
#include <cstring>
#include <string>

#include "core/comms/config.h"
#include "core/flight/extrapolate.h"
#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/nmea_out.h"
#include "core/traffic/range.h"
#include "doctest/doctest.h"
#include "ports/link.h"
#include "test/support/nmea_checksum.h"

using namespace skyblip;
using namespace skyblip::protocol;

// The fields after the sentence name: one comma stands in front of each of them.
static int data_fields(const std::string& sentence) {
    const std::string body = sentence.substr(0, sentence.find('*'));
    return static_cast<int>(std::count(body.begin(), body.end(), ','));
}

static model::Ownship own_at(int32_t latitude_1e7, int32_t longitude_1e7, int32_t altitude_m) {
    model::Ownship own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.latitude_1e7 = latitude_1e7;
    own.longitude_1e7 = longitude_1e7;
    own.altitude_hae_mm = altitude_m * 1000;
    return own;
}

TEST_CASE("nmea: checksum appended correctly") {
    char buffer[128];
    int written = format_pgrmz(buffer, sizeof(buffer), 3500, true);
    std::string sentence(buffer, written);
    CHECK(sentence.rfind("$PGRMZ,", 0) == 0);
    CHECK(checksum_ok(sentence));
    CHECK(sentence.substr(sentence.size() - 2) == "\r\n");
}

// Field 3 of $PGRMZ is the fix dimension Garmin defines for it (1=no fix,
// 2=2D, 3=3D), not a constant: both SoftRF forks send exactly these two values
// off exactly this test (src/protocol/data/NMEA.cpp:
// "isValidGNSSFix() ? '3' : '1'"), because own.fix_valid carries no 2D/3D
// distinction of its own - the same reduction format_pflau's GPS field already
// makes.
TEST_CASE("nmea: PGRMZ field 3 is the fix dimension, not a hardcoded constant") {
    char buffer[128];
    int written = format_pgrmz(buffer, sizeof(buffer), 3500, true);
    std::string sentence(buffer, written);
    CHECK(sentence.rfind("$PGRMZ,3500,F,3*", 0) == 0);

    written = format_pgrmz(buffer, sizeof(buffer), 3500, false);
    sentence.assign(buffer, written);
    CHECK(sentence.rfind("$PGRMZ,3500,F,1*", 0) == 0);
}

TEST_CASE("nmea: relative geometry, target due north is +north, ~0 east") {
    auto own = own_at(481000000, 81000000, 1000);
    model::AircraftObservation observation{};
    observation.position_valid = true;
    observation.latitude_1e7 = own.latitude_1e7 + 10000000;  // +1 deg lat ~ 111 km north
    observation.longitude_1e7 = own.longitude_1e7;
    observation.altitude_m = 1200;
    int32_t north_m, east_m, up_m;
    REQUIRE(flight::relative_north_east_up(own, observation, north_m, east_m, up_m));
    CHECK(north_m > 100000);
    CHECK(std::abs(east_m) < 100);
    CHECK(up_m == 200);
}

// Longitude was differenced raw, so a neighbour one meridian over was 40,000 km east and refused.
TEST_CASE("nmea: a target across the antimeridian is metres away, not the width of the earth") {
    auto own = own_at(0, 1799999000, 1000);
    model::AircraftObservation observation{};
    observation.position_valid = true;
    observation.latitude_1e7 = own.latitude_1e7;
    observation.longitude_1e7 = -1799999000;
    observation.altitude_m = 1000;
    int32_t north_m, east_m, up_m;
    REQUIRE(flight::relative_north_east_up(own, observation, north_m, east_m, up_m));
    CHECK(north_m == 0);
    // 0.0002 deg of longitude at the equator
    CHECK(east_m == doctest::Approx(22).epsilon(0.05));
}

TEST_CASE("nmea: PFLAA carries the id, the relative position and the checksum") {
    auto own = own_at(481000000, 81000000, 1000);
    model::AircraftObservation observation{};
    observation.position_valid = true;
    observation.address = 0xC5D804;
    observation.address_mapping_table = 6;  // FLARM -> IDType 2
    observation.aircraft_category = 4;
    observation.speed_valid = true;
    observation.speed_q = 120;
    observation.climb_valid = true;
    observation.climb_e8 = 16;
    observation.track_c9 = 128;
    observation.latitude_1e7 = own.latitude_1e7 + 100000;  // ~1.1 km north
    observation.longitude_1e7 = own.longitude_1e7 + 100000;
    observation.altitude_m = 1150;
    char buffer[128];
    int written = format_pflaa(buffer, sizeof(buffer), own, observation, 2, nullptr);
    REQUIRE(written > 0);
    std::string sentence(buffer, written);
    CHECK(sentence.rfind("$PFLAA,2,", 0) == 0);
    CHECK(sentence.find(",2,C5D804,") != std::string::npos);  // IDType 2, hex id
    CHECK(checksum_ok(sentence));
}

// OGN hangs the name off the ID field behind a '!' (oss/nrf52-ogn-tracker/src/lookout.h:144).
TEST_CASE("nmea: PFLAA carries a known callsign behind the address, as OGN's does") {
    auto own = own_at(481000000, 81000000, 1000);
    model::AircraftObservation observation{};
    observation.position_valid = true;
    observation.address = 0xC5D804;
    observation.address_mapping_table = 0x06;
    observation.latitude_1e7 = own.latitude_1e7 + 100000;
    observation.longitude_1e7 = own.longitude_1e7;
    observation.altitude_m = 1000;

    char buffer[128];
    int written = format_pflaa(buffer, sizeof(buffer), own, observation, 0, "D-KXYZ");
    REQUIRE(written > 0);
    std::string named(buffer, written);
    CHECK(named.find(",2,C5D804!D-KXYZ,") != std::string::npos);
    CHECK(checksum_ok(named));

    written = format_pflaa(buffer, sizeof(buffer), own, observation, 0, "");
    REQUIRE(written > 0);
    std::string unnamed(buffer, written);
    CHECK(unnamed.find(",2,C5D804,") != std::string::npos);
    CHECK(checksum_ok(unnamed));
}

TEST_CASE("nmea: PFLAA keeps its eleven fields whatever callsign a receiver accepts") {
    auto own = own_at(481000000, 81000000, 1000);
    model::AircraftObservation observation{};
    observation.position_valid = true;
    observation.address = 0xC5D804;
    observation.address_mapping_table = 0x06;
    observation.latitude_1e7 = own.latitude_1e7 + 100000;
    observation.longitude_1e7 = own.longitude_1e7;
    observation.altitude_m = 1000;

    for (int character = 1; character < 256; character++) {
        AdslPacket packet{};
        from_own_callsign(packet, 0x123456, 58, "D-KXYZ");
        packet.information_text()[1] = static_cast<char>(character);
        char name[AdslPacket::kInformationTextBytes + 1] = {0};
        if (callsign_of(packet, name, sizeof(name)) == 0) continue;

        CAPTURE(character);
        char buffer[128];
        const int written = format_pflaa(buffer, sizeof(buffer), own, observation, 0, name);
        REQUIRE(written > 0);
        const std::string sentence(buffer, written);
        CHECK(data_fields(sentence) == 11);
        CHECK(sentence.find('$', 1) == std::string::npos);
        CHECK(sentence.find('*') == sentence.rfind('*'));
        CHECK(checksum_ok(sentence));
    }
}

TEST_CASE("nmea: PFLAA returns 0 without own position") {
    model::Ownship own{};  // no fix
    model::AircraftObservation observation{};
    observation.position_valid = true;
    char buffer[128];
    CHECK(format_pflaa(buffer, sizeof(buffer), own, observation, 0, nullptr) == 0);
}

TEST_CASE("nmea: PFLAU reports the receive count, the GPS state and the threat") {
    auto own = own_at(481000000, 81000000, 1000);
    model::AircraftObservation threat{};
    threat.address = 0x112233;
    char buffer[128];
    int written = format_pflau(buffer, sizeof(buffer), own, true, 5, &threat, 3, 45, -50, 800);
    std::string sentence(buffer, written);
    CHECK(sentence.rfind("$PFLAU,5,1,2,1,3,", 0) == 0);
    CHECK(sentence.find("112233") != std::string::npos);
    // A threat off the left wing is a negative bearing, not 315.
    const int back = format_pflau(buffer, sizeof(buffer), own, true, 5, &threat, 3, -45, -50, 800);
    CHECK(std::string(buffer, back).find(",-45,") != std::string::npos);
    CHECK(checksum_ok(sentence));
}

// FTD-012 defines the GPS field as 0 no fix, 1 a 3D fix on the ground, 2 a 3D fix moving.
TEST_CASE("nmea: PFLAU's GPS field is the flight state, as FLARM defines it") {
    auto own = own_at(481000000, 81000000, 1000);
    own.flight_state = static_cast<uint8_t>(flight::FlightState::Ground);
    CHECK(pflau_gps(own) == 1);

    own.flight_state = static_cast<uint8_t>(flight::FlightState::Airborne);
    CHECK(pflau_gps(own) == 2);

    // Unknown is what every bad solution reports, and it is not a claim to be standing still.
    own.flight_state = static_cast<uint8_t>(flight::FlightState::Unknown);
    CHECK(pflau_gps(own) == 2);

    own.fix_valid = false;
    CHECK(pflau_gps(own) == 0);
}

// XCSoar names value 1 GPS_2D (src/FLARM/Status.hpp), so a device on the apron shows a 2D fix.
TEST_CASE("nmea: PFLAU GPS 1 on the ground reads as a 2D fix on XCSoar, and is still sent") {
    auto own = own_at(481000000, 81000000, 1000);
    own.flight_state = static_cast<uint8_t>(flight::FlightState::Ground);
    char buffer[128];
    const int written = format_pflau(buffer, sizeof(buffer), own, true, 0, nullptr, 0, 0, 0, 0);
    CHECK(std::string(buffer, written).rfind("$PFLAU,0,1,1,1,0,", 0) == 0);

    // SoftRF sends GNSS_STATUS_3D_MOVING on any fix (lyusupov .../data/NMEA.cpp:547), we do not.
    own.flight_state = static_cast<uint8_t>(flight::FlightState::Airborne);
    const int air = format_pflau(buffer, sizeof(buffer), own, true, 0, nullptr, 0, 0, 0, 0);
    CHECK(std::string(buffer, air).rfind("$PFLAU,0,1,2,1,0,", 0) == 0);
}

// SoftRF sends the tenth field empty rather than dropping it: "$PFLAU,0,1,2,1,0,,0,,,*4C".
TEST_CASE("nmea: PFLAU carries all ten FTD-012 fields, empty sky or not") {
    auto own = own_at(481000000, 81000000, 1000);
    own.flight_state = static_cast<uint8_t>(flight::FlightState::Airborne);
    char buffer[128];

    const int quiet = format_pflau(buffer, sizeof(buffer), own, true, 0, nullptr, 0, 0, 0, 0);
    const std::string nothing_out_there(buffer, quiet);
    CHECK(nothing_out_there.rfind("$PFLAU,0,1,2,1,0,,0,,,*4C", 0) == 0);
    CHECK(data_fields(nothing_out_there) == 10);
    CHECK(checksum_ok(nothing_out_there));

    model::AircraftObservation threat{};
    threat.address = 0x112233;
    const int seen = format_pflau(buffer, sizeof(buffer), own, true, 1, &threat, 2, -45, -50, 800);
    const std::string with_a_threat(buffer, seen);
    CHECK(data_fields(with_a_threat) == 10);
    CHECK(checksum_ok(with_a_threat));
}

// A unit that hears everything and speaks to nobody is what this field is for.
TEST_CASE("nmea: PFLAU's TX field follows the transmitter's own gate, not the clock's validity") {
    auto own = own_at(481000000, 81000000, 1000);
    char buffer[128];
    const int silent = format_pflau(buffer, sizeof(buffer), own, false, 2, nullptr, 0, 0, 0, 0);
    CHECK(std::string(buffer, silent).rfind("$PFLAU,2,0,2,1,0,", 0) == 0);

    const int sending = format_pflau(buffer, sizeof(buffer), own, true, 2, nullptr, 0, 0, 0, 0);
    CHECK(std::string(buffer, sending).rfind("$PFLAU,2,1,2,1,0,", 0) == 0);
}

TEST_CASE("nmea: category mapping ADS-L glider -> ALP-TAS 1") {
    CHECK(adsl_category_to_alptas_type(4) == 0x1);  // glider
    CHECK(adsl_category_to_alptas_type(3) == 0x3);  // heli
}

// oss/SoftRF-moshe-braner/.../libraries/OGN/ads-l.h:657-658 is the source for
// both branches: "if (AddrType==5) AddrType=1; else AddrType=2; // SkyDemon
// only accepts 1 or 2". Every ADS-L address table value that is not ICAO (5) -
// our own self-minted range 0-4, FLARM 6, OGN 7, and anything reserved beyond
// it - lands on IDType 2 rather than IDType 0: 0 draws nothing at all on
// SkyDemon, which is the one failure worth never causing again, and between
// the two legal values a self-minted address is a closer lie as FLARM
// (transient, self-assigned) than as ICAO (permanent, registry-issued).
TEST_CASE(
    "nmea: address_mapping_table_to_id_type never answers 0 - SkyDemon draws nothing for it") {
    CHECK(address_mapping_table_to_id_type(5) == 1);  // ICAO
    CHECK(address_mapping_table_to_id_type(6) == 2);  // FLARM
    CHECK(address_mapping_table_to_id_type(7) == 2);  // OGN, folded into the FLARM bucket
    CHECK(address_mapping_table_to_id_type(0) == 2);  // self-minted/random, ditto
    CHECK(address_mapping_table_to_id_type(4) == 2);  // top of the self-minted range
    for (int table = 0; table < 64; table++)
        CHECK(address_mapping_table_to_id_type(static_cast<uint8_t>(table)) != 0);
}

// own.utc_s is a Unix epoch with no calendar fields of its own; the numbers
// below are 2026-08-02T01:01:01Z, 1785628800 + 3661s (1785628800 is the same
// 2026-08-02T00:00:00Z base test/support/product_rig.h flies every product
// case from), chosen so the date and time fields below are hand-checkable
// rather than merely re-deriving the same arithmetic the formatter uses.
TEST_CASE("nmea: GPRMC/GPGGA carry own-ship's own absolute position") {
    auto own = own_at(481234500, -81234500, 1000);
    own.utc_s = 1785628800u + 3661u;
    own.altitude_msl_mm = 950000;
    own.satellites = 8;
    own.hdop_e2 = 120;
    own.speed_mm_s = 40000;
    own.track_cdeg = 9000;

    char buffer[128];
    const int rmc_written = format_gprmc(buffer, sizeof(buffer), own);
    REQUIRE(rmc_written > 0);
    const std::string rmc(buffer, rmc_written);
    CHECK(checksum_ok(rmc));
    CHECK(rmc.substr(rmc.size() - 2) == "\r\n");
    // Time, status, lat/lon (format_nmea_latitude/lon, pinned separately in
    // test_util.cpp), ground speed in knots to one decimal (40 m/s * 3600/1852
    // = 77.75), track, date, and a mode indicator every consumer accepts.
    CHECK(rmc == "$GPRMC,010101,A,4807.4070,N,00807.4070,W,77.8,90,020826,,,A*76\r\n");

    const int gga_written = format_gpgga(buffer, sizeof(buffer), own);
    REQUIRE(gga_written > 0);
    const std::string gga(buffer, gga_written);
    CHECK(checksum_ok(gga));
    // Fix quality 1, 8 satellites, HDOP 1.20, MSL altitude 950 m, and a geoid
    // separation of altitude_m - altitude_msl_m = 1000 - 950 = 50 m (GGA field 11, so an
    // app that wants HAE can add it back).
    CHECK(gga == "$GPGGA,010101,4807.4070,N,00807.4070,W,1,08,1.20,950,M,50,M,,*67\r\n");
}

TEST_CASE("nmea: GPRMC/GPGGA emit nothing without both a fix and a UTC time") {
    char buffer[128];
    model::Ownship own = own_at(481000000, 81000000, 1000);

    model::Ownship no_fix = own;
    no_fix.fix_valid = false;
    CHECK(format_gprmc(buffer, sizeof(buffer), no_fix) == 0);
    CHECK(format_gpgga(buffer, sizeof(buffer), no_fix) == 0);

    model::Ownship no_utc = own;
    no_utc.utc_valid = false;
    CHECK(format_gprmc(buffer, sizeof(buffer), no_utc) == 0);
    CHECK(format_gpgga(buffer, sizeof(buffer), no_utc) == 0);
}

TEST_CASE("nmea: the widest sentence these can produce still fits the narrowest payload") {
    // There is no sender on this endpoint yet. When there is, it goes through
    // ports::Link::send like the other two, which refuses a frame longer than the
    // negotiated payload - so what these can produce at their widest is a budget
    // worth pinning now rather than discovering on someone's iPhone.
    model::Ownship own = own_at(-899999999, -1799999999, -999);
    own.utc_valid = true;
    model::AircraftObservation observation{};
    observation.position_valid = true;
    observation.address = 0xFFFFFF;
    observation.address_mapping_table = 0x06;
    observation.latitude_1e7 = 899999999;
    observation.longitude_1e7 = 1799999999;
    observation.altitude_m = 99999;
    observation.track_c9 = 511;
    observation.speed_q = 65535;
    observation.climb_e8 = 32767;
    observation.speed_valid = true;
    observation.climb_valid = true;
    observation.aircraft_category = 11;
    observation.flight_state = 2;

    char buffer[256];
    const int traffic = format_pflaa(buffer, sizeof(buffer), own, observation, 3, "ABCDEFGHIJKLMN");
    CHECK(traffic > 0);
    CHECK(traffic <= comms::kSmallestSupportedPayloadBytes);
    const int status =
        format_pflau(buffer, sizeof(buffer), own, true, 99, &observation, 3, -180, -99999, 999999);
    CHECK(status > 0);
    CHECK(status <= comms::kSmallestSupportedPayloadBytes);
    // And neither fits what BLE merely guarantees, which is why a sender here
    // cannot assume a frame is a sentence.
    CHECK(traffic > ports::kMinimumLinkPayloadBytes);

    own.speed_mm_s = 16383750;
    own.track_cdeg = 35999;
    own.satellites = 255;
    own.hdop_e2 = 65535;
    own.altitude_msl_mm = -9999000;
    own.utc_s = 4294967295u;
    const int rmc = format_gprmc(buffer, sizeof(buffer), own);
    CHECK(rmc > 0);
    CHECK(rmc <= comms::kSmallestSupportedPayloadBytes);
    const int gga = format_gpgga(buffer, sizeof(buffer), own);
    CHECK(gga > 0);
    CHECK(gga <= comms::kSmallestSupportedPayloadBytes);

    // $LK8EX1 at its widest: every field clamped to the largest value that is
    // not its own sentinel, and the battery in the two-digit-plus-1000 form.
    Lk8Ex1 widest{};
    widest.pressure_pa = 4000000000u;
    widest.altitude_m = 2000000;
    widest.vario_cm_s = -2000000;
    widest.temperature_c = -2000;
    widest.battery_percent = 255;
    widest.pressure_valid = widest.altitude_valid = widest.vario_valid = true;
    widest.temperature_valid = widest.battery_valid = true;
    const int lk8ex1_written = format_lk8ex1(buffer, sizeof(buffer), widest);
    CHECK(lk8ex1_written > 0);
    CHECK(lk8ex1_written <= comms::kSmallestSupportedPayloadBytes);
}
