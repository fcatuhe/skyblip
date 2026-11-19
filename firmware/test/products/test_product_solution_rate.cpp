// The Go asks its L76K for quarters, because the ADS-L it transmits dates positions to them.
#include <cstdint>

#include "doctest/doctest.h"
#include "test/support/product_rig.h"

using namespace skyblip;

static_assert(go::kSolutionRate == ports::SolutionRate::Quarters,
              "the Go transmits ADS-L, which G.1.1 dates to a quarter second");

TEST_CASE("product: the Go asks its receiver for quarters once, and it solves on them") {
    Rig rig;
    rig.platform.retune_gnss_port(true);
    REQUIRE(rig.setup() == Status::Ok);
    for (uint32_t now_ms = 100; now_ms <= 15000; now_ms += 10) {
        rig.platform.chips().gnss.tick(now_ms);
        rig.platform.clock().set_milliseconds(now_ms);
        rig.product.run_pass(now_ms);
    }

    CHECK(rig.platform.chips().gnss.quarter_rate_asked == 1);
    CHECK(rig.platform.chips().gnss.solution_period_ms == 250);
    CHECK(rig.product.board().gnss().solving_quarters());
}
