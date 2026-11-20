#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_DIE_TEMPERATURE_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_DIE_TEMPERATURE_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

#include <cstdint>

#include "ports/die_temperature.h"

namespace skyblip::platform::zephyr {

// INFO: fc 20nov26 through temp_nrf5, never the register, because the BLE controller also reads the
// die and the driver serialises the two
class DieTemperature : public ports::DieTemperature {
   public:
    bool ready() const { return sensor_ != nullptr && device_is_ready(sensor_); }

    bool read(int16_t& die_dc) override {
        if (!ready()) return false;
        if (sensor_sample_fetch(sensor_) != 0) return false;
        struct sensor_value reading{};
        if (sensor_channel_get(sensor_, SENSOR_CHAN_DIE_TEMP, &reading) != 0) return false;
        const int32_t measured_dc = reading.val1 * 10 + reading.val2 / 100000;
        // INFO: fc 20nov26 past the nRF52840's -40 to +105 C operating range the sensor is broken,
        // not the unit cold or hot
        if (measured_dc < -500 || measured_dc > 1250) return false;
        die_dc = static_cast<int16_t>(measured_dc);
        return true;
    }

   private:
#if DT_HAS_COMPAT_STATUS_OKAY(nordic_nrf_temp)
    const struct device* sensor_{DEVICE_DT_GET_ONE(nordic_nrf_temp)};
#else
    const struct device* sensor_{nullptr};
#endif
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
