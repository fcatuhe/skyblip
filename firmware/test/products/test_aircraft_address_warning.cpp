// When the self-test page lists the Go's own transponder as possible traffic.
#include "doctest/doctest.h"
#include "products/skyblip_go/aircraft_address_warning.h"
#include "products/skyblip_go/settings.h"

using namespace skyblip;
using namespace skyblip::go;

TEST_CASE(
    "aircraft address warning: it stands only while paired and neither the Go nor Bravo holds "
    "one" *
    doctest::test_suite("main")) {
    AircraftAddressWarning warning;
    CHECK_FALSE(warning.stands(kAircraftAddressUnset));

    warning.paired(/*bravo_aircraft_address_set=*/false);
    CHECK(warning.stands(kAircraftAddressUnset));
    CHECK_FALSE(warning.stands(0x3C6586));

    warning.paired(/*bravo_aircraft_address_set=*/true);
    CHECK_FALSE(warning.stands(kAircraftAddressUnset));

    warning.paired(/*bravo_aircraft_address_set=*/false);
    warning.unpaired();
    CHECK_FALSE(warning.stands(kAircraftAddressUnset));
}
