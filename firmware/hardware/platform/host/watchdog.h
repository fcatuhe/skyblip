#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_WATCHDOG_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_WATCHDOG_H

#include "core/util/result.h"

namespace skyblip::platform::host {

class Watchdog {
   public:
    Status arm() {
        armed_ = true;
        return Status::Ok;
    }

    void feed() {}
    bool armed() const { return armed_; }

   private:
    bool armed_{false};
};

}  // namespace skyblip::platform::host

#endif
