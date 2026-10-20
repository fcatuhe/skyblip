#include "core/flight/slip.h"

#include "core/util/intmath.h"

namespace skyblip::flight {

bool slip_from_specific_force(const SpecificForce& force, int16_t& out_mg) {
    const int32_t resultant = resultant_mg(force);
    if (resultant < kResultantFloorMg) return false;
    out_mg = static_cast<int16_t>(-force.right_mg * 1000 / resultant);
    return true;
}

void SlipBall::observe(const SpecificForce& force, uint32_t at_ms) {
    int16_t sample_mg = 0;
    if (!slip_from_specific_force(force, sample_mg)) return;
    measured_mg_ = sample_mg;

    const int32_t sampled = sample_mg * kIndicatedDampingSamples;
    damped_accumulator_ =
        seen_ ? damped_accumulator_ + (sampled - damped_accumulator_) / kIndicatedDampingSamples
              : sampled;
    last_ms_ = at_ms;
    seen_ = true;
}

bool SlipBall::valid(uint32_t now_ms) const {
    return seen_ && now_ms - last_ms_ < kIndicatedStaleMs;
}

int16_t SlipBall::damped_mg() const {
    return static_cast<int16_t>(divide_rounded(damped_accumulator_, kIndicatedDampingSamples));
}

}  // namespace skyblip::flight
