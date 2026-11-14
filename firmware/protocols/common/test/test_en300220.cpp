// A logarithm a tenth of a dB off moves the CCA threshold a device declares under EN 300 220-1.
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "doctest/doctest.h"
#include "protocols/common/en300220.h"

using namespace skyblip;

TEST_CASE("en300220: ten_log10_centi rounds to the hundredth of a dB the library gives") {
    for (uint32_t value = 1; value < 300000; value += value / 64 + 1) {
        const double expected = 1000.0 * std::log10(static_cast<double>(value));
        CHECK(std::abs(en300220::ten_log10_centi(value) - expected) <= 0.51);
    }
    CHECK(en300220::ten_log10_centi(1) == 0);
    CHECK(en300220::ten_log10_centi(10) == 1000);
    CHECK(en300220::ten_log10_centi(1000) == 3000);
    CHECK(en300220::ten_log10_centi(0xFFFFFFFFu) == 9633);
}

// Table 32's own example: a 16 kHz receiver "corresponds to a receiver sensitivity of -105 dBm".
TEST_CASE("en300220: the sensitivity limit of Table 32 matches the clause's example") {
    CHECK(en300220::sensitivity_centi_dbm(16000) == -10496);
    CHECK(en300220::sensitivity_centi_dbm(200000) == -9399);
}

// Table 45: "With antenna gain of -5 dB, the CCA threshold shall be ... increased by 5 dB".
TEST_CASE("en300220: the CCA threshold sits 15 dB over sensitivity, less the antenna gain") {
    constexpr uint32_t kChannelHz = 200000;
    CHECK(en300220::cca_threshold_centi_dbm(kChannelHz, 25, 0) == -7899);
    CHECK(en300220::cca_threshold_centi_dbm(kChannelHz, 100, 0) == -8299);
    const int32_t lossy = en300220::cca_threshold_centi_dbm(kChannelHz, 25, -500);
    CHECK(lossy == -7399);
    CHECK(lossy - 500 == en300220::cca_threshold_centi_dbm(kChannelHz, 25, 0));
    CHECK(en300220::cca_threshold_centi_dbm(kChannelHz, 25, 300) == -8199);
}

TEST_CASE("en300220: a level at the threshold is busy, one below it is clear") {
    CHECK(en300220::channel_clear(-8001, -8000));
    CHECK_FALSE(en300220::channel_clear(-8000, -8000));
    CHECK_FALSE(en300220::channel_clear(-7000, -8000));
}
