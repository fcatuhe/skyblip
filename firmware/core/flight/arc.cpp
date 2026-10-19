#include "core/flight/arc.h"

#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::flight {

namespace {

constexpr int64_t kCentiDegreeMillisecondsPerTurn = 36000000;

}  // namespace

int32_t turn_angle16(int16_t turn_cdps, int32_t elapsed_ms) {
    return static_cast<int32_t>(
        div_round(static_cast<int64_t>(turn_cdps) * elapsed_ms * kAngle16PerTurn,
                  kCentiDegreeMillisecondsPerTurn));
}

int16_t clamped_turn_cdps(int32_t turn_cdps) {
    if (turn_cdps > kTurnCeilingCdps) return kTurnCeilingCdps;
    if (turn_cdps < -kTurnCeilingCdps) return -kTurnCeilingCdps;
    return static_cast<int16_t>(turn_cdps);
}

Motion motion_of(const model::Ownship& own) {
    Motion motion{};
    motion.speed_mm_s = own.speed_mm_s;
    motion.track_cdeg = own.track_cdeg;
    motion.turn_cdps = clamped_turn_cdps(own.turn_cdps);
    motion.turning = true;
    motion.climb_mm_s = own.climb_mm_s;
    motion.climbing = own.climb_valid;
    return motion;
}

Motion motion_of(const model::AircraftObservation& observation, int16_t turn_cdps,
                 bool turn_valid) {
    Motion motion{};
    motion.speed_mm_s =
        observation.speed_valid ? to_mm_s(QuarterMetresPerSecond(observation.speed_q)).value : 0;
    motion.track_cdeg = to_centi_degrees(Cordic9(observation.track_c9)).value;
    motion.turn_cdps = turn_valid ? clamped_turn_cdps(turn_cdps) : 0;
    motion.turning = turn_valid;
    motion.climb_mm_s = to_mm_s(EighthMetresPerSecond(observation.climb_e8)).value;
    motion.climbing = observation.climb_valid;
    return motion;
}

Arc::Arc(const Motion& motion, uint32_t step_ms)
    : north_mm_(static_cast<int64_t>(motion.north_m) * kMillimetresPerMetre),
      east_mm_(static_cast<int64_t>(motion.east_m) * kMillimetresPerMetre),
      up_mm_(static_cast<int64_t>(motion.up_m) * kMillimetresPerMetre),
      climb_mm_s_(motion.climbing ? motion.climb_mm_s : 0),
      step_ms_(static_cast<int32_t>(step_ms)) {
    const int16_t angle = to_angle16(CentiDegrees(motion.track_cdeg));
    velocity_north_mm_s_ = static_cast<int32_t>(
        div_round<int64_t>(static_cast<int64_t>(motion.speed_mm_s) * cosine(angle), kTrigOne));
    velocity_east_mm_s_ = static_cast<int32_t>(
        div_round<int64_t>(static_cast<int64_t>(motion.speed_mm_s) * sine(angle), kTrigOne));
    const int32_t half_turn =
        motion.turning ? turn_angle16(clamped_turn_cdps(motion.turn_cdps), step_ms_ / 2) : 0;
    const int16_t half = static_cast<int16_t>(static_cast<uint16_t>(half_turn));
    half_cosine_ = cosine(half);
    half_sine_ = sine(half);
}

Position Arc::here() const {
    Position position{};
    position.north_m = static_cast<int32_t>(div_round<int64_t>(north_mm_, kMillimetresPerMetre));
    position.east_m = static_cast<int32_t>(div_round<int64_t>(east_mm_, kMillimetresPerMetre));
    position.up_m = static_cast<int32_t>(div_round<int64_t>(up_mm_, kMillimetresPerMetre));
    return position;
}

void Arc::rotate_half() {
    const int64_t north = velocity_north_mm_s_, east = velocity_east_mm_s_;
    velocity_north_mm_s_ = static_cast<int32_t>(
        div_round<int64_t>(north * half_cosine_ - east * half_sine_, kTrigOne));
    velocity_east_mm_s_ = static_cast<int32_t>(
        div_round<int64_t>(north * half_sine_ + east * half_cosine_, kTrigOne));
}

Position Arc::advance() {
    rotate_half();
    north_mm_ += div_round<int64_t>(static_cast<int64_t>(velocity_north_mm_s_) * step_ms_,
                                    kMillisecondsPerSecond);
    east_mm_ += div_round<int64_t>(static_cast<int64_t>(velocity_east_mm_s_) * step_ms_,
                                   kMillisecondsPerSecond);
    up_mm_ +=
        div_round<int64_t>(static_cast<int64_t>(climb_mm_s_) * step_ms_, kMillisecondsPerSecond);
    rotate_half();
    return here();
}

}  // namespace skyblip::flight
