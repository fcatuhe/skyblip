#ifndef SKYBLIP_PORTS_CENTRAL_H
#define SKYBLIP_PORTS_CENTRAL_H

#include <cstdint>

#include "core/events/central.h"
#include "core/util/result.h"
#include "core/util/span.h"

namespace skyblip::ports {

// INFO: fc 11nov26 how much of the radio a scan takes: it listens window_us out of every period_us
struct ScanDuty {
    uint32_t window_us;
    uint32_t period_us;
};

class Central {
   public:
    virtual ~Central() = default;

    // INFO: fc 11nov26 reports only names starting with name_prefix, each advertiser once a scan
    virtual Status scan(const ScanDuty& duty, const char* name_prefix) = 0;
    virtual void stop_scan() = 0;

    // INFO: fc 11nov26 one peer at a time: Up once subscribed, Down when it goes or never came
    virtual Status connect(const events::BluetoothAddress& address, const ScanDuty& duty) = 0;
    virtual void disconnect() = 0;

    // INFO: fc 11nov26 a write wider than the payload Up carried is refused, never shortened
    virtual Status write(ConstByteSpan bytes) = 0;
};

}  // namespace skyblip::ports

#endif
