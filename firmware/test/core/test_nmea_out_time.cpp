// The time an EFB reads beside own-ship's position, at the rate the receiver now solves.
#include <string>

#include "core/model/ownship.h"
#include "core/protocol/nmea_out.h"
#include "doctest/doctest.h"
#include "test/support/nmea_checksum.h"
#include "test/support/nmea_traffic.h"

using namespace skyblip;
using namespace skyblip::protocol;

// A quarter solution stamped with its whole second would put the position 750 ms off its time.
TEST_CASE("nmea: GPRMC/GPGGA carry a quarter solution's fraction, and a whole second none") {
    auto own = own_at(481234500, -81234500, 1000);
    own.utc_s = 1785628800u + 3661u;
    own.utc_ms = 750;

    char buffer[128];
    const std::string rmc(buffer, format_gprmc(buffer, sizeof(buffer), own));
    CHECK(checksum_ok(rmc));
    CHECK(rmc.rfind("$GPRMC,010101.75,A,", 0) == 0);
    const std::string gga(buffer, format_gpgga(buffer, sizeof(buffer), own));
    CHECK(checksum_ok(gga));
    CHECK(gga.rfind("$GPGGA,010101.75,", 0) == 0);

    own.utc_ms = 0;
    const std::string whole(buffer, format_gprmc(buffer, sizeof(buffer), own));
    CHECK(whole.rfind("$GPRMC,010101,A,", 0) == 0);
}
