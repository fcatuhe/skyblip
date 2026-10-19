#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_CLOCK_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_CLOCK_H

#include "ports/clock.h"

namespace skyblip::platform::host {

class Clock : public ports::Clock {
   public:
    uint32_t millis() const override { return static_cast<uint32_t>(now_us_ / 1000); }
    uint64_t micros() const override { return now_us_; }
    // INFO: fc 13sep26 millis() wraps and micros() does not (ports/clock.h), so a wrap is a step
    void set_millis(uint32_t now_ms) {
        const uint64_t elapsed_ms = now_us_ / 1000;
        now_us_ += static_cast<uint64_t>(now_ms - static_cast<uint32_t>(elapsed_ms)) * 1000;
    }
    void set_micros(uint64_t now_us) { now_us_ = now_us; }
    void advance(uint32_t elapsed_ms) { now_us_ += static_cast<uint64_t>(elapsed_ms) * 1000; }
    void advance_us(uint64_t elapsed_us) { now_us_ += elapsed_us; }

   private:
    uint64_t now_us_{0};
};

}

#endif
