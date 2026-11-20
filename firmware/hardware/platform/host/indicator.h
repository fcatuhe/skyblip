#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_INDICATOR_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_INDICATOR_H

#include <cstdint>

#include "ports/indicator.h"

namespace skyblip::platform::host {

// INFO: fc 20nov26 counts show() calls, because an LED re-driven every pass costs a register write
// a hundred times a second
class Indicator : public ports::Indicator {
   public:
    void show(indication::Colour colour) override {
        lamp_ = colour;
        shows_++;
        if (colour != indication::Colour::None) lightings_++;
        parked_ = false;
    }

    void park() override {
        lamp_ = indication::Colour::None;
        parked_ = true;
        parks_++;
    }

    indication::Colour lamp() const { return lamp_; }
    bool lit() const { return lamp_ != indication::Colour::None; }
    bool parked() const { return parked_; }
    uint32_t shows() const { return shows_; }
    uint32_t lightings() const { return lightings_; }
    uint32_t parks() const { return parks_; }

   private:
    indication::Colour lamp_{indication::Colour::None};
    uint32_t shows_{0};
    uint32_t lightings_{0};
    uint32_t parks_{0};
    bool parked_{false};
};

}  // namespace skyblip::platform::host

#endif
