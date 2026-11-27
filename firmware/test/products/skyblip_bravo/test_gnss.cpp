// What Bravo publishes from its AG3335, which has no satellites page to feed.
#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

// The receiver sends GSV from power-on until $PAIR062,3,0 lands, and its count stayed frozen.
TEST_CASE("bravo: a GSV from the receiver puts no satellites in view on the bus") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.gnss_on_air = true;
    rig.run_for(3000);

    rig.platform.gnss().sentence_every[models::Ag3335::kGsv] = 1;
    rig.run_for(3000);

    REQUIRE(rig.state().own.fix_valid);
    CHECK(rig.state().gnss.sky.count() == 0);
}
