#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_CENTRAL_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_CENTRAL_H
#if defined(__ZEPHYR__)

#include <cstdint>

#include "core/events/central.h"
#include "core/events/link.h"
#include "ports/central.h"

namespace skyblip::platform::zephyr {

class Central : public ports::Central {
   public:
    Status scan(const ports::ScanDuty& duty, const char* name_prefix) override;
    void stop_scan() override;
    Status connect(const events::BluetoothAddress& address, const ports::ScanDuty& duty) override;
    void disconnect() override;
    Status write(ConstByteSpan bytes) override;

    bool pop_event(events::CentralEvent& out);
    bool pop_rx(events::RxFrame& out);
};

Central& central();

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
