// The 1% of any hour the band allows a transmitter, ADS-L's declared channel-access route.
#include <cstdint>

#include "doctest/doctest.h"
#include "protocols/adsl/transmit.h"

using namespace skyblip;

// E2. The 1% duty cycle of EN 300 220-2 V3.3.1 Table 4 is the declared route, and the only refusal.

TEST_CASE("channel: the declared route is the duty cycle, and nothing else refuses a burst") {
    CHECK(adsl::AirTime::kLimitPermille == 10);
    CHECK(adsl::AirTime::kBudgetMs == adsl::AirTime::kWindowMs / 100);

    adsl::AirTime air;
    air.spend(0, adsl::AirTime::kBudgetMs);
    CHECK_FALSE(air.may_spend(0, adsl::kAirTimeMs));
    // A second later the hour still holds every millisecond of it.
    CHECK_FALSE(air.may_spend(1000, adsl::kAirTimeMs));
}

TEST_CASE("channel: air time is counted per rolling hour and leaves it again") {
    adsl::AirTime air;
    for (uint32_t second = 0; second < 3600; second++) air.spend(second * 1000, adsl::kAirTimeMs);

    CHECK(air.bursts() == 3600);
    CHECK(air.total_ms() == 3600 * adsl::kAirTimeMs);
    CHECK(air.window_ms(3599000) == 18000);
    // One 5 ms burst a second is 0.5%: half of what the band allows.
    CHECK(air.permille(3599000) == 5);
    CHECK(air.permille(3599000) * 2 == adsl::AirTime::kLimitPermille);
    // An hour after the last burst the window is empty, and the total is not.
    CHECK(air.window_ms(3599000 + adsl::AirTime::kWindowMs) == 0);
    CHECK(air.total_ms() == 18000);
}

TEST_CASE("channel: the hour's allowance is 1% of it, and the counter stops at the figure") {
    CHECK(adsl::AirTime::kBudgetMs == adsl::AirTime::kWindowMs / 100);
    CHECK(adsl::AirTime::kBudgetMs == 36000);

    adsl::AirTime air;
    uint32_t refused = 0;
    for (uint32_t now_ms = 0; now_ms < adsl::AirTime::kWindowMs; now_ms += 100) {
        if (air.may_spend(now_ms, adsl::kAirTimeMs))
            air.spend(now_ms, adsl::kAirTimeMs);
        else
            refused++;
    }
    CHECK(air.window_ms(adsl::AirTime::kWindowMs - 100) == adsl::AirTime::kBudgetMs);
    CHECK(refused > 0);
    CHECK_FALSE(air.may_spend(adsl::AirTime::kWindowMs - 100, adsl::kAirTimeMs));
}

// The regulatory wrap. EN 300 220-2 V3.3.1 Table 4 band M is 1% of any hour, and
// the hour that straddles the wrap is an hour like any other. Buckets numbered
// now_ms / kBucketMs cannot do this: that number restarts at zero at the wrap and
// 2^32 ms is not a whole number of minutes either, so the ring dropped everything
// it held at the wrap and the following hour could spend the allowance a second
// time. The ring turns by elapsed time now.
TEST_CASE("channel: the rolling duty-cycle hour spans the 49.7-day wrap") {
    adsl::AirTime air;
    // Half an hour of the design rate up to the wrap, half an hour after it.
    const uint32_t start = 0xFFFFFFFFu - 1800u * 1000u + 1u;
    for (uint32_t i = 0; i < 3600; i++) air.spend(start + i * 1000u, adsl::kAirTimeMs);

    const uint32_t last = start + 3599u * 1000u;
    CHECK(air.bursts() == 3600);
    CHECK(air.total_ms() == 3600 * adsl::kAirTimeMs);
    // Every one of the 3600 bursts is inside the hour that ends at the last one,
    // whichever side of zero it was spent on. Before the fix this read 9000: the
    // half hour before the wrap had been forgotten.
    CHECK(air.window_ms(last) == 18000);
    CHECK(air.permille(last) == 5);
    // So the budget is still the band's, and a device at twice the design rate
    // through the wrap is still refused at 1%.
    adsl::AirTime hot;
    uint32_t spent = 0;
    for (uint32_t i = 0; i < 36000; i++) {
        const uint32_t at = start + i * 100u;
        if (!hot.may_spend(at, adsl::kAirTimeMs)) continue;
        hot.spend(at, adsl::kAirTimeMs);
        spent += adsl::kAirTimeMs;
    }
    CHECK(spent <= adsl::AirTime::kBudgetMs);
    CHECK(hot.window_ms(start + 35999u * 100u) == adsl::AirTime::kBudgetMs);
}

TEST_CASE("channel: air time spent before the wrap leaves the hour after it") {
    adsl::AirTime air;
    air.spend(0xFFFFF000u, 1000);
    CHECK(air.window_ms(0xFFFFF000u) == 1000);
    // Still inside the hour, 59 minutes and 55 seconds later, past the wrap.
    CHECK(air.window_ms(0xFFFFF000u + adsl::AirTime::kWindowMs - 5000u) == 1000);
    // And out of it, five seconds after that.
    CHECK(air.window_ms(0xFFFFF000u + adsl::AirTime::kWindowMs) == 0);
    CHECK(air.total_ms() == 1000);
}

// A device parked for a day and then flown: the ring has no bucket the hour can
// reach, so it holds nothing, and the total it has spent since boot is untouched.
TEST_CASE("channel: a silence longer than the window empties it and keeps the total") {
    adsl::AirTime air;
    air.spend(1000, 5);
    air.spend(1000 + 24u * 3600u * 1000u, 5);
    CHECK(air.window_ms(1000 + 24u * 3600u * 1000u) == 5);
    CHECK(air.total_ms() == 10);
    CHECK(air.bursts() == 2);
}
