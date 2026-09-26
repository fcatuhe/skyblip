#ifndef SKYBLIP_CORE_POWER_DUTY_H
#define SKYBLIP_CORE_POWER_DUTY_H

#include <cstdint>

namespace skyblip::power {

class OnTime {
   public:
    void observe(bool on, uint32_t now_ms) {
        if (on_) ms_ += now_ms - at_ms_;
        at_ms_ = now_ms;
        on_ = on;
    }

    uint32_t ms() const { return ms_; }

   private:
    uint32_t ms_{0};
    uint32_t at_ms_{0};
    bool on_{false};
};

}  // namespace skyblip::power

#endif
