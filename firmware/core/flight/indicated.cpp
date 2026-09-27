#include "core/flight/indicated.h"

#include "core/util/intmath.h"

namespace skyblip::flight {

void IndicatedRate::observe(int32_t measured, uint32_t now_ms) {
    const int64_t sampled = static_cast<int64_t>(measured) * kScale;
    if (seen_) {
        const int64_t dt = now_ms - last_ms_;
        damped_acc_ +=
            div_round<int64_t>((sampled - damped_acc_) * dt, dt + kIndicatedRateDampingMs);
    } else {
        damped_acc_ = sampled;
    }
    last_ms_ = now_ms;
    seen_ = true;
}

int32_t IndicatedRate::value() const {
    if (!seen_) return 0;
    return static_cast<int32_t>(div_round<int64_t>(damped_acc_, kScale));
}

}  // namespace skyblip::flight
