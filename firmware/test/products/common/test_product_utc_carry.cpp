// A silent receiver's last UTC second counts on the uptime clock until it may have drifted.
#include "core/timing/slot.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

// A receiver with no pulse names one second and then says nothing more.
void dated_once_then_silent(Rig& rig) {
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.pps().set_locked(false);
    rig.run(0, 950);
    gnss::GnssSolution dated{};
    dated.utc_valid = true;
    dated.utc_s = kBaseUtcS;
    dated.sentences = ++rig.fix_updates;
    rig.product.bus().gnss.push(dated);
    rig.run(1000, 1950);
}

}  // namespace

TEST_CASE("product: a silent receiver's UTC second counts on the uptime clock") {
    Rig rig;
    dated_once_then_silent(rig);
    REQUIRE(rig.state().clock.utc_s == 0);
    CHECK(rig.state().traffic_now_s(1950) == kBaseUtcS);

    rig.run(2000, 601000, 1000);
    CHECK(rig.state().own.utc_valid);
    CHECK(rig.state().traffic_now_s(601000) == kBaseUtcS + 600);
}

TEST_CASE("product: a silent receiver's UTC is withdrawn once the uptime clock may have drifted") {
    Rig rig;
    dated_once_then_silent(rig);
    const uint32_t last_ms = 1000 + timing::kUtcReferenceMaxAgeMs;
    rig.run(2000, last_ms, 1000);
    REQUIRE(rig.state().own.utc_valid);

    rig.run(last_ms + 1000, last_ms + 2000, 1000);
    CHECK_FALSE(rig.state().own.utc_valid);
    CHECK_FALSE(rig.state().clock.utc_valid);
    CHECK(rig.state().traffic_now_s(last_ms + 2000) == (last_ms + 2000) / 1000);

    // The next solution dates the clock again.
    gnss::GnssSolution dated{};
    dated.utc_valid = true;
    dated.utc_s = kBaseUtcS + 2002;
    dated.sentences = ++rig.fix_updates;
    rig.product.bus().gnss.push(dated);
    rig.run(last_ms + 3000, last_ms + 3000);
    CHECK(rig.state().own.utc_valid);
    CHECK(rig.state().traffic_now_s(last_ms + 3000) == kBaseUtcS + 2002);
}
