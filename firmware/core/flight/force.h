#ifndef SKYBLIP_CORE_FLIGHT_FORCE_H
#define SKYBLIP_CORE_FLIGHT_FORCE_H

#include <cstdint>

namespace skyblip::flight {

struct SpecificForce {
    int16_t right_mg{0};
    int16_t up_mg{0};
    int16_t aft_mg{0};
};

constexpr int16_t kLevelFlightMg = 1000;
constexpr int32_t kResultantFloorMg = 200;

constexpr uint32_t kIndicatedStaleMs = 2000;
constexpr int32_t kIndicatedDampingSamples = 8;

int32_t resultant_mg(const SpecificForce& force);

}  // namespace skyblip::flight

#endif
