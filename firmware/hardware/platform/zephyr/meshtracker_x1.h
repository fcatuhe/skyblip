#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_MESHTRACKER_X1_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_MESHTRACKER_X1_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <cstdint>

#include "core/util/result.h"
#include "hardware/platform/zephyr/bluetooth_tag.h"
#include "hardware/platform/zephyr/clock.h"
#include "hardware/platform/zephyr/die_temperature.h"
#include "hardware/platform/zephyr/io.h"
#include "hardware/platform/zephyr/watchdog.h"
#include "ports/capabilities.h"

namespace skyblip::platform::zephyr {

// TODO: fc 10nov26 the SoC's parts only, until #170 gives the X1 a devicetree with its own
class MeshtrackerX1 {
   public:
    Status begin() {
        if (!device_is_ready(gpio0_) || !device_is_ready(gpio1_)) return Status::Down;
        return Status::Ok;
    }

    io::Gpio& gpio() { return gpio_; }
    io::Delay& delay() { return delay_; }
    zephyr::Clock& clock() { return clock_; }
    zephyr::DieTemperature& die_temperature() { return die_temperature_; }
    zephyr::Watchdog& watchdog() { return watchdog_; }
    static uint16_t bluetooth_tag() { return zephyr::bluetooth_tag(); }

    ports::Capabilities capabilities() const {
        ports::Capabilities found = ports::Capability::None;
        if (die_temperature_.ready()) found |= ports::Capability::DieTemperature;
        return found;
    }

   private:
    const struct device* gpio0_{DEVICE_DT_GET(DT_NODELABEL(gpio0))};
    const struct device* gpio1_{DEVICE_DT_GET(DT_NODELABEL(gpio1))};

    zephyr::Clock clock_{};
    Gpio gpio_{gpio0_, gpio1_};
    Delay delay_{};
    zephyr::DieTemperature die_temperature_{};
    zephyr::Watchdog watchdog_{};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
