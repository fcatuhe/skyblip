#ifndef SKYBLIP_CORE_FLIGHT_SLIP_H
#define SKYBLIP_CORE_FLIGHT_SLIP_H

#include <cstdint>

#include "core/flight/force.h"

namespace skyblip::flight {

constexpr int32_t kSlipFullScaleMg = 200;

bool slip_from_specific_force(const SpecificForce& force, int16_t& out_mg);

class SlipBall {
   public:
    void update(const SpecificForce& force, uint32_t at_ms);
    bool valid(uint32_t now_ms) const;
    int16_t mg() const;
    int16_t measured_mg() const { return measured_mg_; }

   private:
    int32_t damped_acc_{0};
    int16_t measured_mg_{0};
    uint32_t last_ms_{0};
    bool seen_{false};
};

}  // namespace skyblip::flight

#endif
