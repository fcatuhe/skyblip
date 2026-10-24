#ifndef SKYBLIP_CORE_POWER_DUTY_H
#define SKYBLIP_CORE_POWER_DUTY_H

#include <cstdint>

namespace skyblip::power {

class OnTime {
   public:
    void observe(bool on, uint32_t now_ms) {
        if (on_) on_ms_ += now_ms - observed_at_ms_;
        observed_at_ms_ = now_ms;
        on_ = on;
    }

    uint32_t on_ms() const { return on_ms_; }

   private:
    uint32_t on_ms_{0};
    uint32_t observed_at_ms_{0};
    bool on_{false};
};

}  // namespace skyblip::power

#endif
