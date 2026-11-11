// Own pressure altitude, which an ADS-B target with no GNSS height is placed against (#161).
#include <cstdlib>

#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

TEST_CASE("bravo: the SPA06 publishes own pressure altitude, dated by its sample") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.barometer().set_altitude_mm(1200000);
    rig.seconds_with_fix(3);

    const model::Ownship& own = rig.state().own;
    REQUIRE(own.pressure_altitude_valid);
    CHECK(std::abs(own.pressure_altitude_mm - 1200000) < 1000);
    CHECK(rig.now_ms - own.pressure_altitude_at_ms < 2000);
}

TEST_CASE("bravo: own pressure altitude expires once the SPA06 stops answering") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(3);
    REQUIRE(rig.state().own.pressure_altitude_valid);

    rig.platform.barometer().answers = false;
    rig.seconds_with_fix(5);
    CHECK_FALSE(rig.state().own.pressure_altitude_valid);
    CHECK(rig.product.board().barometer_faults() > 0);

    rig.platform.barometer().answers = true;
    rig.seconds_with_fix(3);
    CHECK(rig.state().own.pressure_altitude_valid);
}

TEST_CASE("bravo: a unit with no barometer flies, and never claims a pressure altitude") {
    BravoRig rig(
        ports::without(platform::host::MeshtrackerX1::kFullyFitted, ports::Capability::Barometer));
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(ports::has(rig.product.degraded(), ports::Capability::Barometer));
    rig.seconds_with_fix(3);
    CHECK_FALSE(rig.state().own.pressure_altitude_valid);
}
