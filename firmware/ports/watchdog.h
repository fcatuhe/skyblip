// ports/watchdog.h: capability port: the hardware watchdog. runtime/ decides
// WHETHER the dog may be fed (runtime/watchdog.h); this only carries the answer
// to the silicon.
#ifndef SKYBLIP_PORTS_WATCHDOG_H
#define SKYBLIP_PORTS_WATCHDOG_H

#include <cstdint>

#include "core/util/result.h"

namespace skyblip::ports {

class Watchdog {
   public:
    virtual ~Watchdog() = default;

    // Arm with the longest the loop may go unheard.
    //
    // INFO: hk 02aug26 an nRF52 watchdog cannot be disarmed once started: CRV,
    // RREN and CONFIG are blocked while RUNSTATUS is set (nRF52840 PS v1.8
    // §6.34). There is deliberately no stop() here, because there is no stop.
    virtual Status arm(uint32_t timeout_ms) {
        (void)timeout_ms;
        return Status::Unsupported;
    }

    virtual void feed() {}
    virtual bool armed() const { return false; }
};

}  // namespace skyblip::ports

#endif
