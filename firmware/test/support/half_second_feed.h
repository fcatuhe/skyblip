// Harness, not a test: a 2 Hz receiver's solutions fed onto the product's bus, each when it lands.
#ifndef SKYBLIP_TEST_SUPPORT_HALF_SECOND_FEED_H
#define SKYBLIP_TEST_SUPPORT_HALF_SECOND_FEED_H

#include <cstdint>

#include "core/gnss/nmea.h"
#include "test/support/product_rig.h"

namespace skyblip {

constexpr uint32_t kHalfSecondFeedMs = 500;
constexpr uint32_t kFeedLatencyMs = 70;

struct Solution {
    uint32_t second{0};
    uint32_t utc_ms{0};
    int32_t speed_mm_s{0};
    uint32_t late_ms{kFeedLatencyMs};
    bool fix_valid{true};
    int32_t stamped_ahead_s{0};
    uint16_t delivery_ms{0};
    bool backlogged{false};
    bool relabeled{false};
};

// One solution, landing late_ms after the instant it names, its latitude a step per solution.
inline void deliver(Rig& rig, const Solution& delivered, uint32_t& updates) {
    gnss::GnssSolution solution{};
    solution.fix_valid = delivered.fix_valid;
    solution.utc_valid = true;
    solution.utc_s = kBaseUtcS + delivered.second + delivered.stamped_ahead_s;
    solution.utc_ms = static_cast<uint16_t>(delivered.utc_ms);
    solution.latitude_1e7 = 485000000 + static_cast<int32_t>(updates) * 100;
    solution.longitude_1e7 = 85000000;
    solution.altitude_hae_mm = 1000000;
    solution.altitude_msl_valid = true;
    solution.speed_mm_s = delivered.speed_mm_s;
    solution.track_cdeg = 0;
    solution.speed_valid = true;
    solution.track_valid = true;
    solution.hdop_e2 = 100;
    solution.vdop_e2 = 150;
    solution.satellites = 10;
    solution.delivery_ms = delivered.delivery_ms;
    solution.backlogged = delivered.backlogged;
    solution.relabeled = delivered.relabeled;
    solution.sentences = ++updates;
    rig.product.bus().gnss.push(solution);
    const uint32_t at_ms = delivered.second * 1000 + delivered.utc_ms + delivered.late_ms;
    rig.run(at_ms, at_ms);
}

inline void solve(Rig& rig, uint32_t second, uint32_t half, int32_t speed_mm_s, uint32_t& updates) {
    deliver(rig, Solution{second, half * kHalfSecondFeedMs, speed_mm_s}, updates);
}

}  // namespace skyblip

#endif
