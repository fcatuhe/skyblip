#ifndef SKYBLIP_CORE_FLIGHT_ARC_H
#define SKYBLIP_CORE_FLIGHT_ARC_H

#include <cstdint>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"

namespace skyblip::flight {

struct Motion {
    int32_t north_m{0};
    int32_t east_m{0};
    int32_t up_m{0};
    int32_t speed_mm_s{0};
    int32_t track_cdeg{0};
    int16_t turn_cdps{0};
    int32_t climb_mm_s{0};
    bool turning{false};
    bool climbing{false};
};

Motion motion_of(const model::Ownship& own);
// INFO: fc 14sep26 ADS-L carries no turn rate (G.1.8), so a target's is core/traffic's estimate
Motion motion_of(const model::AircraftObservation& observation, int16_t turn_cdps, bool turn_valid);

struct Position {
    int32_t north_m{0};
    int32_t east_m{0};
    int32_t up_m{0};
};

constexpr int16_t kTurnCeilingDps = 30;
constexpr int16_t kTurnCeilingCdps = kTurnCeilingDps * 100;

int16_t clamped_turn_cdps(int32_t turn_cdps);
int32_t turn_angle16(int16_t turn_cdps, int32_t dt_ms);

class Arc {
   public:
    Arc(const Motion& motion, uint32_t step_ms);

    Position here() const;
    Position advance();

   private:
    void rotate_half();

    int64_t north_mm_;
    int64_t east_mm_;
    int64_t up_mm_;
    int32_t velocity_north_mm_s_;
    int32_t velocity_east_mm_s_;
    int32_t climb_mm_s_;
    int32_t step_ms_;
    int16_t half_cos_;
    int16_t half_sin_;
};

}  // namespace skyblip::flight

#endif
