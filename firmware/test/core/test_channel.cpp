// How loud the site is, and the 1% of any hour the band allows a transmitter.
#include "core/timing/channel.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"

using namespace skyblip::timing;

// E1. Nothing gates a burst on the channel, but a pilot still reads how loud the site is.

TEST_CASE("channel: the floor starts at the seed and walks to what the receiver hears") {
    NoiseFloor floor;
    CHECK(floor.dbm() == NoiseFloor::kSeedDbm);
    CHECK(floor.samples() == 0);

    for (int i = 0; i < 500; i++) floor.sample(-118);
    CHECK(floor.dbm() == -118);
    CHECK(floor.samples() == 500);

    // And back up again: the average has no memory of having been quiet.
    for (int i = 0; i < 500; i++) floor.sample(-96);
    CHECK(floor.dbm() == -96);
}

// A burst passing through does not become the floor. A minute of a jammer does.
TEST_CASE("channel: one loud sample barely moves the average, a site full of them moves it all") {
    NoiseFloor floor;
    floor.sample(-40);
    CHECK(floor.dbm() <= NoiseFloor::kSeedDbm + 4);

    NoiseFloor site;
    for (int i = 0; i < 200; i++) site.sample(-85);
    CHECK(site.dbm() == -85);
}

// The chip has no averaging block and no non-LoRa activity mode, so a level is a run of reads.
TEST_CASE("channel: one level is a window of readings, not the instant one landed on") {
    CHECK(ChannelLevel::kWindowUs == 160);
    CHECK(ChannelLevel::kSamples >= 2);
    CHECK(ChannelLevel::kSampleSpacingUs * (ChannelLevel::kSamples - 1) >= ChannelLevel::kWindowUs);
    CHECK(ChannelLevel::kSamples <= ChannelLevel::kMaxSamples);
}

TEST_CASE("channel: a window is averaged as power, not as decibels") {
    const int8_t flat[ChannelLevel::kSamples] = {-100, -100, -100, -100, -100,
                                                 -100, -100, -100, -100};
    CHECK(ChannelLevel::mean_dbm(flat, ChannelLevel::kSamples) == -100);

    // One eighth of a window at -60 and the rest 40 dB down: the mean POWER is
    // 1/9 of the loud sample, which is 9.5 dB below it. The mean of the READINGS
    // would be -95.6 dBm, which is a channel nobody is using.
    const int8_t burst[ChannelLevel::kSamples] = {-100, -100, -100, -100, -60,
                                                  -100, -100, -100, -100};
    CHECK(ChannelLevel::mean_dbm(burst, ChannelLevel::kSamples) == -69);

    // Rounding is toward the louder decibel, so a loud site never reads quiet.
    const int8_t pair[2] = {-70, -70};
    CHECK(ChannelLevel::mean_dbm(pair, 2) == -70);
    const int8_t half[2] = {-70, -127};
    CHECK(ChannelLevel::mean_dbm(half, 2) == -73);

    // No reading at all is not a quiet channel.
    CHECK(ChannelLevel::mean_dbm(flat, 0) == 0);
    CHECK(ChannelLevel::mean_dbm(nullptr, ChannelLevel::kSamples) == 0);
}

// A floor fed single reads reports the gaps between bursts as the site's level.
TEST_CASE("channel: the floor hears the window, not the instant one read landed on") {
    const int8_t window[ChannelLevel::kSamples] = {-110, -110, -110, -110, -55,
                                                   -110, -110, -110, -110};
    NoiseFloor windowed, instant;
    for (int i = 0; i < 400; i++) {
        windowed.sample(ChannelLevel::mean_dbm(window, ChannelLevel::kSamples));
        instant.sample(window[0]);
    }
    CHECK(instant.dbm() == window[0]);
    CHECK(windowed.dbm() > instant.dbm() + 40);
}

// E2. The 1% duty cycle of EN 300 220-2 V3.3.1 Table 4 is the declared route, and the only refusal.

TEST_CASE("channel: the declared route is the duty cycle, and nothing else refuses a burst") {
    CHECK(AirTime::kLimitPermille == 10);
    CHECK(AirTime::kBudgetMs == AirTime::kWindowMs / 100);

    AirTime air;
    air.spend(0, AirTime::kBudgetMs);
    CHECK_FALSE(air.may_spend(0, Transmitter::kAirTimeMs));
    // A second later the hour still holds every millisecond of it.
    CHECK_FALSE(air.may_spend(1000, Transmitter::kAirTimeMs));
}

TEST_CASE("channel: air time is counted per rolling hour and leaves it again") {
    AirTime air;
    for (uint32_t second = 0; second < 3600; second++)
        air.spend(second * 1000, Transmitter::kAirTimeMs);

    CHECK(air.bursts() == 3600);
    CHECK(air.total_ms() == 3600 * Transmitter::kAirTimeMs);
    CHECK(air.window_ms(3599000) == 18000);
    // One 5 ms burst a second is 0.5%: half of what the band allows.
    CHECK(air.permille(3599000) == 5);
    CHECK(air.permille(3599000) * 2 == AirTime::kLimitPermille);
    // An hour after the last burst the window is empty, and the total is not.
    CHECK(air.window_ms(3599000 + AirTime::kWindowMs) == 0);
    CHECK(air.total_ms() == 18000);
}

TEST_CASE("channel: the hour's allowance is 1% of it, and the counter stops at the figure") {
    CHECK(AirTime::kBudgetMs == AirTime::kWindowMs / 100);
    CHECK(AirTime::kBudgetMs == 36000);

    AirTime air;
    uint32_t refused = 0;
    for (uint32_t t = 0; t < AirTime::kWindowMs; t += 100) {
        if (air.may_spend(t, Transmitter::kAirTimeMs))
            air.spend(t, Transmitter::kAirTimeMs);
        else
            refused++;
    }
    CHECK(air.window_ms(AirTime::kWindowMs - 100) == AirTime::kBudgetMs);
    CHECK(refused > 0);
    CHECK_FALSE(air.may_spend(AirTime::kWindowMs - 100, Transmitter::kAirTimeMs));
}

// The regulatory wrap. EN 300 220-2 V3.3.1 Table 4 band M is 1% of any hour, and
// the hour that straddles the wrap is an hour like any other. Buckets numbered
// now_ms / kBucketMs cannot do this: that number restarts at zero at the wrap and
// 2^32 ms is not a whole number of minutes either, so the ring dropped everything
// it held at the wrap and the following hour could spend the allowance a second
// time. The ring turns by elapsed time now.
TEST_CASE("channel: the rolling duty-cycle hour spans the 49.7-day wrap") {
    AirTime air;
    // Half an hour of the design rate up to the wrap, half an hour after it.
    const uint32_t start = 0xFFFFFFFFu - 1800u * 1000u + 1u;
    for (uint32_t i = 0; i < 3600; i++) air.spend(start + i * 1000u, Transmitter::kAirTimeMs);

    const uint32_t last = start + 3599u * 1000u;
    CHECK(air.bursts() == 3600);
    CHECK(air.total_ms() == 3600 * Transmitter::kAirTimeMs);
    // Every one of the 3600 bursts is inside the hour that ends at the last one,
    // whichever side of zero it was spent on. Before the fix this read 9000: the
    // half hour before the wrap had been forgotten.
    CHECK(air.window_ms(last) == 18000);
    CHECK(air.permille(last) == 5);
    // So the budget is still the band's, and a device at twice the design rate
    // through the wrap is still refused at 1%.
    AirTime hot;
    uint32_t spent = 0;
    for (uint32_t i = 0; i < 36000; i++) {
        const uint32_t at = start + i * 100u;
        if (!hot.may_spend(at, Transmitter::kAirTimeMs)) continue;
        hot.spend(at, Transmitter::kAirTimeMs);
        spent += Transmitter::kAirTimeMs;
    }
    CHECK(spent <= AirTime::kBudgetMs);
    CHECK(hot.window_ms(start + 35999u * 100u) == AirTime::kBudgetMs);
}

TEST_CASE("channel: air time spent before the wrap leaves the hour after it") {
    AirTime air;
    air.spend(0xFFFFF000u, 1000);
    CHECK(air.window_ms(0xFFFFF000u) == 1000);
    // Still inside the hour, 59 minutes and 55 seconds later, past the wrap.
    CHECK(air.window_ms(0xFFFFF000u + AirTime::kWindowMs - 5000u) == 1000);
    // And out of it, five seconds after that.
    CHECK(air.window_ms(0xFFFFF000u + AirTime::kWindowMs) == 0);
    CHECK(air.total_ms() == 1000);
}

// A device parked for a day and then flown: the ring has no bucket the hour can
// reach, so it holds nothing, and the total it has spent since boot is untouched.
TEST_CASE("channel: a silence longer than the window empties it and keeps the total") {
    AirTime air;
    air.spend(1000, 5);
    air.spend(1000 + 24u * 3600u * 1000u, 5);
    CHECK(air.window_ms(1000 + 24u * 3600u * 1000u) == 5);
    CHECK(air.total_ms() == 10);
    CHECK(air.bursts() == 2);
}
