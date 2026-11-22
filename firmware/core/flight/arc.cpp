#include "core/flight/arc.h"

#include "core/units/units.h"
#include "protocols/common/geometry.h"

namespace skyblip::flight {

namespace {

constexpr int64_t kCentiDegreeMillisecondsPerTurn = 36000000;

}  // namespace

int32_t turn_angle16(int16_t turn_cdps, int32_t elapsed_ms) {
    return static_cast<int32_t>(
        wire::divide_rounded(static_cast<int64_t>(turn_cdps) * elapsed_ms * wire::kAngle16PerTurn,
                             kCentiDegreeMillisecondsPerTurn));
}

int16_t clamped_turn_cdps(int32_t turn_cdps) {
    if (turn_cdps > kTurnCeilingCdps) return kTurnCeilingCdps;
    if (turn_cdps < -kTurnCeilingCdps) return -kTurnCeilingCdps;
    return static_cast<int16_t>(turn_cdps);
}

Arc::Arc(const Motion& motion, int32_t step_s) : step_s_(step_s) {
    const int16_t angle = to_angle16(CentiDegrees(motion.track_cdeg));
    velocity_north_mm_s_ = static_cast<int32_t>(wire::divide_rounded<int64_t>(
        static_cast<int64_t>(motion.speed_mm_s) * wire::cosine(angle), wire::kTrigOne));
    velocity_east_mm_s_ = static_cast<int32_t>(wire::divide_rounded<int64_t>(
        static_cast<int64_t>(motion.speed_mm_s) * wire::sine(angle), wire::kTrigOne));
    const int32_t half_turn = motion.turning ? turn_angle16(clamped_turn_cdps(motion.turn_cdps),
                                                            step_s * kMillisecondsPerSecond / 2)
                                             : 0;
    const int16_t half = static_cast<int16_t>(static_cast<uint16_t>(half_turn));
    half_cosine_ = wire::cosine(half);
    half_sine_ = wire::sine(half);
}

void Arc::rotate_half() {
    const int64_t north = velocity_north_mm_s_, east = velocity_east_mm_s_;
    velocity_north_mm_s_ = static_cast<int32_t>(
        wire::divide_rounded<int64_t>(north * half_cosine_ - east * half_sine_, wire::kTrigOne));
    velocity_east_mm_s_ = static_cast<int32_t>(
        wire::divide_rounded<int64_t>(north * half_sine_ + east * half_cosine_, wire::kTrigOne));
}

Position Arc::advance() {
    rotate_half();
    north_mm_ += velocity_north_mm_s_ * step_s_;
    east_mm_ += velocity_east_mm_s_ * step_s_;
    rotate_half();
    return {wire::divide_rounded(north_mm_, kMillimetresPerMetre),
            wire::divide_rounded(east_mm_, kMillimetresPerMetre)};
}

}  // namespace skyblip::flight
