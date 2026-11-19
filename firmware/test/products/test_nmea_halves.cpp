// The EFB's own-ship sentences at 2 Hz: the whole second they report, and its withdrawal.
#include <cstdint>
#include <string>

#include "doctest/doctest.h"
#include "test/support/half_second_feed.h"
#include "test/support/nmea_stream.h"
#include "test/support/product_rig.h"

using namespace skyblip;

// The pinned case: a fix withdrawn at .500 left the EFB reading the last .000 as a valid fix.
TEST_CASE("nmea: a fix withdrawn at .500 stops the EFB's GPRMC at once") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    uint32_t updates = 0;
    for (uint32_t second = 1; second <= 3; second++)
        for (uint32_t half = 0; half < 2; half++) solve(rig, second, half, 25000, updates);
    REQUIRE(rig.link_up());
    REQUIRE(count_of(rig, "$GPRMC") > 0);

    solve(rig, 4, 0, 25000, updates);
    deliver(rig, Solution{4, 500, 25000, kFeedLatencyMs, false}, updates);
    rig.platform.link().clear();
    rig.run(4900, 8000);
    CHECK(count_of(rig, "$GPRMC") == 0);
    CHECK(count_of(rig, "$GPGGA") == 0);
}
