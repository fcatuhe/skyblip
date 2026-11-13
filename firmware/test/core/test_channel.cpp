// How loud the site is: the noise floor and the channel level, instruments and never gates.
#include "core/timing/channel.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::timing;

// E1. Nothing gates a burst on the channel, but a pilot still reads how loud the site is.

TEST_CASE("channel: the floor starts at the seed and walks to what the receiver hears") {
    NoiseFloor floor;
    CHECK(floor.level_dbm() == NoiseFloor::kSeedDbm);
    CHECK(floor.samples() == 0);

    for (int i = 0; i < 500; i++) floor.sample(-118);
    CHECK(floor.level_dbm() == -118);
    CHECK(floor.samples() == 500);

    // And back up again: the average has no memory of having been quiet.
    for (int i = 0; i < 500; i++) floor.sample(-96);
    CHECK(floor.level_dbm() == -96);
}

// A burst passing through does not become the floor. A minute of a jammer does.
TEST_CASE("channel: one loud sample barely moves the average, a site full of them moves it all") {
    NoiseFloor floor;
    floor.sample(-40);
    CHECK(floor.level_dbm() <= NoiseFloor::kSeedDbm + 4);

    NoiseFloor site;
    for (int i = 0; i < 200; i++) site.sample(-85);
    CHECK(site.level_dbm() == -85);
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
    CHECK(instant.level_dbm() == window[0]);
    CHECK(windowed.level_dbm() > instant.level_dbm() + 40);
}
