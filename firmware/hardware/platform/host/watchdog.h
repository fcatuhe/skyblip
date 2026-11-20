#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_WATCHDOG_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_WATCHDOG_H

#include "ports/watchdog.h"

namespace skyblip::platform::host {

class Watchdog : public ports::Watchdog {
   public:
    Status arm(uint32_t timeout_ms) override {
        timeout_ms_ = timeout_ms;
        arms_++;
        armed_ = true;
        return Status::Ok;
    }

    void feed() override { feeds_++; }
    bool armed() const override { return armed_; }

    uint32_t timeout_ms() const { return timeout_ms_; }
    uint32_t feeds() const { return feeds_; }
    int arms() const { return arms_; }

   private:
    uint32_t timeout_ms_{0};
    uint32_t feeds_{0};
    int arms_{0};
    bool armed_{false};
};

}  // namespace skyblip::platform::host

#endif
