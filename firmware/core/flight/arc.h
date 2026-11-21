#ifndef SKYBLIP_CORE_FLIGHT_ARC_H
#define SKYBLIP_CORE_FLIGHT_ARC_H

#include <cstdint>

namespace skyblip::flight {

struct Motion {
    int32_t speed_mm_s{0};
    int32_t track_cdeg{0};
    int16_t turn_cdps{0};
    bool turning{false};
};

struct Position {
    int32_t north_m{0};
    int32_t east_m{0};
};

constexpr int16_t kTurnCeilingDps = 30;
constexpr int16_t kTurnCeilingCdps = kTurnCeilingDps * 100;

int16_t clamped_turn_cdps(int32_t turn_cdps);
int32_t turn_angle16(int16_t turn_cdps, int32_t elapsed_ms);

class Arc {
   public:
    Arc(const Motion& motion, int32_t step_s);

    Position advance();

   private:
    void rotate_half();

    int32_t north_mm_{0};
    int32_t east_mm_{0};
    int32_t velocity_north_mm_s_;
    int32_t velocity_east_mm_s_;
    int32_t step_s_;
    int16_t half_cosine_;
    int16_t half_sine_;
};

}  // namespace skyblip::flight

#endif
