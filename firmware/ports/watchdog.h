#ifndef SKYBLIP_PORTS_WATCHDOG_H
#define SKYBLIP_PORTS_WATCHDOG_H

#include <cstdint>

#include "core/util/result.h"

namespace skyblip::ports {

class Watchdog {
   public:
    virtual ~Watchdog() = default;

    // INFO: hk 02aug26 no stop(): a running nRF52 watchdog cannot stop (nRF52840 PS v1.8 §6.34)
    virtual Status arm(uint32_t timeout_ms) {
        (void)timeout_ms;
        return Status::Unsupported;
    }

    virtual void feed() {}
    virtual bool armed() const { return false; }
};

}  // namespace skyblip::ports

#endif
