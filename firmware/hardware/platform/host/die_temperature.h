#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_DIE_TEMPERATURE_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_DIE_TEMPERATURE_H

#include <cstdint>

#include "ports/die_temperature.h"

namespace skyblip::platform::host {

class DieTemperature : public ports::DieTemperature {
   public:
    bool read(int16_t& die_dc) override {
        reads_++;
        if (!answers_) return false;
        die_dc = die_dc_;
        return true;
    }

    void hold(int16_t die_dc) {
        die_dc_ = die_dc;
        answers_ = true;
    }
    void refuse() { answers_ = false; }
    int reads() const { return reads_; }

   private:
    int reads_{0};
    int16_t die_dc_{0};
    bool answers_{true};
};

}  // namespace skyblip::platform::host

#endif
