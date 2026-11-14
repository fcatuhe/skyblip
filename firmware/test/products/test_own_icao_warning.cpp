// When the self-test page lists the Go's own transponder as possible traffic.
#include "doctest/doctest.h"
#include "products/skyblip_go/own_icao_warning.h"
#include "products/skyblip_go/settings.h"

using namespace skyblip;
using namespace skyblip::go;

TEST_CASE("own ICAO warning: it stands only while paired and neither the Go nor Bravo holds one") {
    OwnIcaoWarning warning;
    CHECK_FALSE(warning.stands(kOwnIcaoUnset));

    warning.paired(/*bravo_own_icao_set=*/false);
    CHECK(warning.stands(kOwnIcaoUnset));
    CHECK_FALSE(warning.stands(0x3C6586));

    warning.paired(/*bravo_own_icao_set=*/true);
    CHECK_FALSE(warning.stands(kOwnIcaoUnset));

    warning.paired(/*bravo_own_icao_set=*/false);
    warning.unpaired();
    CHECK_FALSE(warning.stands(kOwnIcaoUnset));
}
