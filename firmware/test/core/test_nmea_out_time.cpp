// The time an EFB reads beside own-ship's position, at the rate the receiver now solves.
#include <cstring>
#include <string>

#include "core/model/ownship.h"
#include "core/protocol/nmea_out.h"
#include "doctest/doctest.h"
#include "test/support/nmea_checksum.h"
#include "test/support/nmea_traffic.h"
#include "test/support/timed_fix.h"

using namespace skyblip;
using namespace skyblip::protocol;

// A .500 solution stamped with its whole second would put the position 500 ms off its time.
TEST_CASE("nmea: GPRMC/GPGGA carry a .500 solution's fraction, and a whole second none") {
    auto own = own_at(481234500, -81234500, 1000);
    own.utc_s = kBaseUtcS + 3661u;
    own.utc_ms = 500;

    char buffer[128];
    const std::string rmc(buffer, format_gprmc(buffer, sizeof(buffer), own));
    CHECK(checksum_ok(rmc));
    CHECK(rmc.rfind("$GPRMC,010101.50,A,", 0) == 0);
    const std::string gga(buffer, format_gpgga(buffer, sizeof(buffer), own));
    CHECK(checksum_ok(gga));
    CHECK(gga.rfind("$GPGGA,010101.50,", 0) == 0);

    own.utc_ms = 0;
    const std::string whole(buffer, format_gprmc(buffer, sizeof(buffer), own));
    CHECK(whole.rfind("$GPRMC,010101,A,", 0) == 0);
}

namespace {

// 2026-11-27 12:34:56 UTC
constexpr uint32_t kUtcS = 1795782896;

model::Ownship utc_without_fix() {
    model::Ownship own{};
    own.utc_valid = true;
    own.utc_s = kUtcS;
    own.fix_valid = false;
    return own;
}

}  // namespace

// NMEA 0183 RMC status V is a void position with a valid time and date, as GNSS receivers send it.
TEST_CASE("nmea: a Go with UTC and no fix sends a void RMC carrying the time and date") {
    char sentence[128]{};
    const int bytes = format_gprmc(sentence, sizeof(sentence), utc_without_fix());
    CAPTURE(sentence);
    REQUIRE(bytes > 0);
    CHECK(std::strncmp(sentence, "$GPRMC,123456,V,", 16) == 0);
    CHECK(std::strstr(sentence, ",271126,") != nullptr);
}

// NMEA 0183 GGA quality 0 is no fix, so XCSoar keeps the time and clears the altitude.
TEST_CASE("nmea: a Go with UTC and no fix sends a quality-0 GGA carrying the time alone") {
    model::Ownship own = utc_without_fix();
    own.latitude_1e7 = 481000000;
    own.satellites = 3;
    char sentence[128]{};
    const std::string gga(sentence, format_gpgga(sentence, sizeof(sentence), own));
    CHECK(checksum_ok(gga));
    CHECK(gga.rfind("$GPGGA,123456,,,,,0,03,,,,,,,*", 0) == 0);
    const std::string rmc(sentence, format_gprmc(sentence, sizeof(sentence), own));
    CHECK(checksum_ok(rmc));
    CHECK(rmc.rfind("$GPRMC,123456,V,,,,,,,271126,,,N*", 0) == 0);
}

TEST_CASE("nmea: a Go with no UTC sends no RMC") {
    model::Ownship own = utc_without_fix();
    own.utc_valid = false;
    char sentence[128]{};
    CHECK(format_gprmc(sentence, sizeof(sentence), own) == 0);
}
