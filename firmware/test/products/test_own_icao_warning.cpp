// When the Go warns that its own transponder may be traffic, decided before #178 pairs a Bravo.
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

// A link that drops and comes back in flight must not put the same notice on the glass each time.
TEST_CASE("own ICAO warning: the glass is told once a boot, the first time the warning stands") {
    OwnIcaoWarning warning;
    CHECK_FALSE(warning.take_notice(kOwnIcaoUnset));

    warning.paired(/*bravo_own_icao_set=*/true);
    CHECK_FALSE(warning.take_notice(kOwnIcaoUnset));

    warning.paired(/*bravo_own_icao_set=*/false);
    CHECK_FALSE(warning.take_notice(0x3C6586));
    CHECK(warning.take_notice(kOwnIcaoUnset));
    CHECK_FALSE(warning.take_notice(kOwnIcaoUnset));

    warning.unpaired();
    warning.paired(/*bravo_own_icao_set=*/false);
    CHECK_FALSE(warning.take_notice(kOwnIcaoUnset));
    CHECK(warning.stands(kOwnIcaoUnset));
}
