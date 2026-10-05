#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_BATTERY_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_BATTERY_H
#if defined(__ZEPHYR__)

#include <hal/nrf_power.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include <algorithm>
#include <cstdint>

#include "core/power/battery.h"

namespace skyblip::platform::zephyr {

// The cell, read through the board's divider. Zephyr's voltage-divider driver
// owns the ADC and the ratio (the resistors are a devicetree fact, not a
// constant in code). We own what the millivolts mean, in core/power.
//
// INFO: fc 09mar26 the T-Echo family exposes no charger status pin - SoftRF's
// nRF52 port says the same ("the T-Echo has no ext_power_pin") - so the only
// charge signal is the USB regulator's VBUS detect. Nothing here can measure
// charge current, which is why "charging" ends at a voltage plateau.
class Battery {
   public:
    explicit Battery(const struct device* dev) : dev_(dev) {}

    bool ready() const { return device_is_ready(dev_); }

    // INFO: fc 05oct26 out_mv is what the divider read, refused or not: core/power counts it
    bool read_mv(uint16_t& out_mv) {
        out_mv = 0;
        if (!ready()) return false;
        if (sensor_sample_fetch(dev_) != 0) return false;
        struct sensor_value volts{};
        if (sensor_channel_get(dev_, SENSOR_CHAN_VOLTAGE, &volts) != 0) return false;

        const int32_t millivolts = volts.val1 * 1000 + volts.val2 / 1000;
        out_mv = static_cast<uint16_t>(std::clamp<int32_t>(millivolts, 0, UINT16_MAX));
        return power::plausible_mv(out_mv);
    }

    static bool external_power() { return nrf_power_usbregstatus_vbusdet_get(NRF_POWER); }

   private:
    const struct device* dev_;
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
