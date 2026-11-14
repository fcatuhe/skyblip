// ADS-L 4 SRD-860 issue 2 Subpart D, the data link: the channel access this device declares.
#include <cstdint>

#include "core/timing/channel.h"
#include "doctest/doctest.h"

using namespace skyblip;

// The route taken instead of polite spectrum access, and the only rule that does refuse a burst.
TEST_CASE("ADS-L.4.SRD860.D.3: nothing refuses a burst here but the hour's duty cycle") {
    timing::AirTime air;
    CHECK(timing::AirTime::kLimitPermille == 10u);
    const uint32_t budget_ms = timing::AirTime::kBudgetMs;
    air.spend(0, budget_ms);
    CHECK_FALSE(air.may_spend(0, 1));
    CHECK(air.may_spend(timing::AirTime::kWindowMs + 1, 1));
}
