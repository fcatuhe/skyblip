#include "core/flight/arc.h"

#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::flight {

namespace {

constexpr int64_t kTrigOne = 16384;
constexpr int64_t kTurn16 = 65536;
constexpr int64_t kCentiDegreeMsPerTurn = 36000000;
constexpr int64_t kMmPerM = 1000;

int16_t turn_angle16(int16_t turn_cdps, int32_t dt_ms) {
    return static_cast<int16_t>(static_cast<uint16_t>(
        div_round(static_cast<int64_t>(turn_cdps) * dt_ms * kTurn16, kCentiDegreeMsPerTurn)));
}

}  // namespace

int16_t clamped_turn_cdps(int32_t turn_cdps) {
    if (turn_cdps > kMaxTurnCdps) return kMaxTurnCdps;
    if (turn_cdps < -kMaxTurnCdps) return -kMaxTurnCdps;
    return static_cast<int16_t>(turn_cdps);
}

Motion motion_of(const model::OwnState& own) {
    Motion m{};
    m.speed_mm_s = own.speed_mm_s;
    m.track_cdeg = own.track_cdeg;
    m.turn_cdps = clamped_turn_cdps(own.turn_cdps);
    m.turning = true;
    m.climb_mm_s = own.climb_mm_s;
    m.climbing = own.climb_valid;
    return m;
}

Motion motion_of(const model::AircraftObs& obs, int16_t turn_cdps, bool turn_valid) {
    Motion m{};
    m.speed_mm_s = obs.speed_valid ? to_mm_s(QuarterMetresPerSec(obs.speed_q)).v : 0;
    m.track_cdeg = to_centi_degrees(Cordic9(obs.track_c9)).v;
    m.turn_cdps = turn_valid ? clamped_turn_cdps(turn_cdps) : 0;
    m.turning = turn_valid;
    m.climb_mm_s = to_mm_s(EighthMetresPerSec(obs.climb_e8)).v;
    m.climbing = obs.climb_valid;
    return m;
}

Arc::Arc(const Motion& motion, uint32_t step_ms)
    : north_mm_(static_cast<int64_t>(motion.north_m) * kMmPerM),
      east_mm_(static_cast<int64_t>(motion.east_m) * kMmPerM),
      up_mm_(static_cast<int64_t>(motion.up_m) * kMmPerM),
      climb_mm_s_(motion.climbing ? motion.climb_mm_s : 0),
      step_ms_(static_cast<int32_t>(step_ms)) {
    const int16_t angle = to_angle16(CentiDegrees(motion.track_cdeg));
    vel_north_mm_s_ = static_cast<int32_t>(
        div_round(static_cast<int64_t>(motion.speed_mm_s) * icos(angle), kTrigOne));
    vel_east_mm_s_ = static_cast<int32_t>(
        div_round(static_cast<int64_t>(motion.speed_mm_s) * isin(angle), kTrigOne));
    const int16_t half =
        motion.turning ? turn_angle16(clamped_turn_cdps(motion.turn_cdps), step_ms_ / 2) : 0;
    half_cos_ = icos(half);
    half_sin_ = isin(half);
}

Position Arc::here() const {
    Position p{};
    p.north_m = static_cast<int32_t>(div_round(north_mm_, kMmPerM));
    p.east_m = static_cast<int32_t>(div_round(east_mm_, kMmPerM));
    p.up_m = static_cast<int32_t>(div_round(up_mm_, kMmPerM));
    return p;
}

void Arc::rotate_half() {
    const int64_t n = vel_north_mm_s_, e = vel_east_mm_s_;
    vel_north_mm_s_ = static_cast<int32_t>(div_round(n * half_cos_ - e * half_sin_, kTrigOne));
    vel_east_mm_s_ = static_cast<int32_t>(div_round(n * half_sin_ + e * half_cos_, kTrigOne));
}

Position Arc::advance() {
    rotate_half();
    north_mm_ += div_round<int64_t>(static_cast<int64_t>(vel_north_mm_s_) * step_ms_,
                                    kMillisecondsPerSecond);
    east_mm_ +=
        div_round<int64_t>(static_cast<int64_t>(vel_east_mm_s_) * step_ms_, kMillisecondsPerSecond);
    up_mm_ +=
        div_round<int64_t>(static_cast<int64_t>(climb_mm_s_) * step_ms_, kMillisecondsPerSecond);
    rotate_half();
    return here();
}

}  // namespace skyblip::flight
