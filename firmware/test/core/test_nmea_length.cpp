// How long an NMEA sentence may be, and what gives way when $PFLAA would pass 82 characters.
#include <cstdint>
#include <cstring>
#include <string>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/nmea_out.h"
#include "doctest/doctest.h"
#include "test/support/nmea_checksum.h"
#include "test/support/nmea_traffic.h"

using namespace skyblip;
using namespace skyblip::protocol;

static model::AircraftObservation widest(model::Source source, int32_t latitude_1e7,
                                         int32_t longitude_1e7) {
    auto target = heard(source, latitude_1e7, longitude_1e7, 0);
    target.track_c9 = 511;
    target.speed_q = 65535;
    target.climb_e8 = -32768;
    target.rssi_dbm = -128;
    return target;
}

// Speed and climb at their type's limits, a 6-character height and 3 digits of track.
TEST_CASE("nmea: PFLAA keeps the widest callsign whole under 1000 km, and drops the RSSI first") {
    const auto own = own_at(0, 0, 99999);
    char buffer[128];

    const auto adsl = widest(model::Source::AdslDirect, -89000000, -89000000);
    const int adsl_written = format_pflaa(buffer, sizeof(buffer), own, adsl, 3, "ABCDEFGHIJKLMN");
    CHECK(std::string(buffer, adsl_written) ==
          "$PFLAA,3,-990748,-990748,-99999,2,DD8F12!ABCDEFGHIJKLMN,359,,16384,-4095.9,1*34\r\n");
    CHECK(adsl_written <= kNmeaSentenceCeilingBytes);

    const auto adsb = widest(model::Source::Adsb, -89000000, -89000000);
    const int adsb_written = format_pflaa(buffer, sizeof(buffer), own, adsb, 3, "ABCDEFGH");
    CHECK(std::string(buffer, adsb_written) ==
          "$PFLAA,3,-990748,-990748,-99999,1,3C6586!ABCDEFGH,359,,16384,-4095.9,8,0,1,*14\r\n");
    CHECK(adsb_written <= kNmeaSentenceCeilingBytes);
}

// Half the earth away, north and east take 9 characters each.
TEST_CASE("nmea: PFLAA drops the RSSI, then shortens the callsign, never a field, to fit 82") {
    const auto own = own_at(0, 0, 99999);
    char buffer[128];

    const auto adsl = widest(model::Source::AdslDirect, -899999999, -1799999999);
    const int adsl_written = format_pflaa(buffer, sizeof(buffer), own, adsl, 3, "ABCDEFGHIJKLMN");
    const std::string adsl_sentence(buffer, adsl_written);
    CHECK(adsl_sentence ==
          "$PFLAA,3,-10018800,-20037600,-99999,2,DD8F12!ABCDEFGHIJK,359,,16384,-4095.9,1*7B\r\n");
    CHECK(adsl_written == kNmeaSentenceCeilingBytes);
    CHECK(checksum_ok(adsl_sentence));

    const auto adsb = widest(model::Source::Adsb, -899999999, -1799999999);
    const int adsb_written = format_pflaa(buffer, sizeof(buffer), own, adsb, 3, "ABCDEFGH");
    const std::string adsb_sentence(buffer, adsb_written);
    CHECK(adsb_sentence ==
          "$PFLAA,3,-10018800,-20037600,-99999,1,3C6586!ABCDEF,359,,16384,-4095.9,8,0,1,*1B\r\n");
    CHECK(adsb_written == kNmeaSentenceCeilingBytes);
    CHECK(data_fields(adsb_sentence) == 14);
}

// An alarm level of 255 and an 11-character height are the widest the parameter types carry.
TEST_CASE("nmea: PFLAA fits 82 characters at its types' widest, without callsign or RSSI") {
    const auto own = own_at(0, 0, 0);
    auto adsb = widest(model::Source::Adsb, -899999999, -1799999999);
    adsb.altitude_m = INT32_MIN;
    char buffer[128];
    const int written = format_pflaa(buffer, sizeof(buffer), own, adsb, 255, "ABCDEFGH");
    CHECK(std::string(buffer, written) ==
          "$PFLAA,255,-10018800,-20037600,-2147483648,1,3C6586,359,,16384,-4095.9,8,0,1,*00\r\n");
    CHECK(written == kNmeaSentenceCeilingBytes);
}

// The terminating NUL needs a byte too, so a sentence of n characters needs a buffer of n + 1.
TEST_CASE("nmea: every formatter writes nothing into a buffer too small for its sentence") {
    auto own = own_at(481000000, 81000000, 1000);
    own.utc_s = 1785628800u;
    const auto target = widest(model::Source::Adsb, 481100000, 81100000);
    Lk8Ex1 reading{};
    reading.battery_valid = true;

    using Formatter = int (*)(char*, size_t, const model::Ownship&,
                              const model::AircraftObservation&, const Lk8Ex1&);
    const Formatter formatters[] = {
        [](char* out, size_t capacity, const model::Ownship& ship,
           const model::AircraftObservation& traffic,
           const Lk8Ex1&) { return format_pflaa(out, capacity, ship, traffic, 0, "DLH4AB"); },
        [](char* out, size_t capacity, const model::Ownship& ship,
           const model::AircraftObservation& traffic, const Lk8Ex1&) {
            return format_pflau(out, capacity, ship, true, 1, &traffic, 1, 45, -50, true, 800);
        },
        [](char* out, size_t capacity, const model::Ownship&, const model::AircraftObservation&,
           const Lk8Ex1&) { return format_pgrmz(out, capacity, 3500, true); },
        [](char* out, size_t capacity, const model::Ownship&, const model::AircraftObservation&,
           const Lk8Ex1& lk8) { return format_lk8ex1(out, capacity, lk8); },
        [](char* out, size_t capacity, const model::Ownship& ship,
           const model::AircraftObservation&,
           const Lk8Ex1&) { return format_gprmc(out, capacity, ship); },
        [](char* out, size_t capacity, const model::Ownship& ship,
           const model::AircraftObservation&,
           const Lk8Ex1&) { return format_gpgga(out, capacity, ship); },
    };

    for (const Formatter format : formatters) {
        char roomy[128];
        const int length = format(roomy, sizeof(roomy), own, target, reading);
        REQUIRE(length > 0);
        CAPTURE(std::string(roomy, static_cast<size_t>(length)));

        char tight[128];
        std::memset(tight, '#', sizeof(tight));
        CHECK(format(tight, static_cast<size_t>(length), own, target, reading) == 0);
        CHECK(std::string(tight, sizeof(tight)) == std::string(sizeof(tight), '#'));

        CHECK(format(tight, static_cast<size_t>(length) + 1, own, target, reading) == length);
        CHECK(std::string(tight, static_cast<size_t>(length) + 1) ==
              std::string(roomy, static_cast<size_t>(length) + 1));
    }
}
