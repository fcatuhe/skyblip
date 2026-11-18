// The EFB's own-ship sentences at 4 Hz: the whole second they report, and its withdrawal.
#include <cstdint>
#include <string>

#include "doctest/doctest.h"
#include "test/support/nmea_stream.h"
#include "test/support/product_rig.h"
#include "test/support/quarter_feed.h"

using namespace skyblip;

// The pinned case: a fix withdrawn at .750 left the EFB reading the last .000 as a valid fix.
TEST_CASE("nmea: a fix withdrawn on a quarter stops the EFB's GPRMC at once") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    uint32_t updates = 0;
    for (uint32_t second = 1; second <= 3; second++)
        for (uint32_t quarter = 0; quarter < 4; quarter++)
            solve(rig, second, quarter, 25000, updates);
    REQUIRE(rig.link_up());
    REQUIRE(count_of(rig, "$GPRMC") > 0);

    solve(rig, 4, 0, 25000, updates);
    solve(rig, 4, 1, 25000, updates);
    deliver(rig, Solution{4, 750, 25000, kQuarterFeedLatencyMs, false}, updates);
    rig.platform.link().clear();
    rig.run(4900, 8000);
    CHECK(count_of(rig, "$GPRMC") == 0);
    CHECK(count_of(rig, "$GPGGA") == 0);
}
