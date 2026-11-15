// Bravo's status dump: the adsb group says where each frame the LR2021 handed up went.
#include "doctest/doctest.h"
#include "products/skyblip_bravo/services/diagnostics.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

TEST_CASE("bravo: the adsb group splits refused frames from the aircraft folded before the table") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(1);
    AdsbAir corrupted = adsb_air(adsb_airborne_position(0x3C6586, 0, 48.51, 8.5, false, 1800));
    corrupted.chips[10] ^= 0x0C;
    rig.platform.radio().receive_air(corrupted.chips, sizeof(corrupted.chips));
    rig.hear(adsb_frame(0x3C6586, 0, 28));
    rig.hear_aircraft(0x3C6586, 2000);
    rig.hear_aircraft(0x4CA2B1, 3000);
    rig.seconds_with_fix(1);

    bravo::DiagnosticsCollector<Bravo>().step(rig.product, rig.now_ms);
    const comms::Diagnostics& read = rig.product.config_link().config().diagnostics();
    CHECK(read.adsb_crc == 1);
    CHECK(read.adsb_unsupported == 1);
    CHECK(read.adsb_folded == 4);
    CHECK(read.adsb_aircraft == 2);
}
