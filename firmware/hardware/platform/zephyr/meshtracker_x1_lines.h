#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_MESHTRACKER_X1_LINES_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_MESHTRACKER_X1_LINES_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/printk.h>

#include <cstdint>

#include "core/util/result.h"
#include "hardware/platform/zephyr/annunciator.h"
#include "hardware/platform/zephyr/battery.h"
#include "hardware/platform/zephyr/bluetooth_tag.h"
#include "hardware/platform/zephyr/clock.h"
#include "hardware/platform/zephyr/die_temperature.h"
#include "hardware/platform/zephyr/indicator.h"
#include "hardware/platform/zephyr/io.h"
#include "hardware/platform/zephyr/kvstore.h"
#include "hardware/platform/zephyr/watchdog.h"
#include "ports/capabilities.h"

namespace skyblip::platform::zephyr {

// INFO: fc 12nov26 the X1's own lines, driven alone by the smoke image and held by Bravo's platform
class MeshtrackerX1Lines {
   public:
    Status begin() {
        if (begin_inputs() != Status::Ok) return Status::Down;
        storage_mounted_ = key_value_.begin() == Status::Ok;
        annunciator_.begin();
        indicator_.begin();
        return Status::Ok;
    }

    // INFO: fc 12nov26 the button and the charger lines alone, which a refused wake needs and no
    // more
    Status begin_inputs() {
        if (!device_is_ready(gpio0_) || !device_is_ready(gpio1_)) return Status::Down;
        if (!input(button_, "button") || !input(charge_status_, "charge status") ||
            !input(charge_done_, "charge done"))
            return Status::Down;
        return Status::Ok;
    }

    io::Gpio& gpio() { return gpio_; }
    io::Delay& delay() { return delay_; }
    io::I2c& i2c(io::BusId) { return sensor_i2c_; }

    zephyr::Clock& clock() { return clock_; }
    zephyr::KeyValueStore& key_value() { return key_value_; }
    zephyr::Annunciator& annunciator() { return annunciator_; }
    zephyr::Indicator& indicator() { return indicator_; }
    zephyr::Battery& battery() { return battery_; }
    zephyr::DieTemperature& die_temperature() { return die_temperature_; }
    zephyr::Watchdog& watchdog() { return watchdog_; }
    static uint16_t bluetooth_tag() { return zephyr::bluetooth_tag(); }
    bool storage_mounted() const { return storage_mounted_; }

    bool button_down() { return gpio_pin_get_dt(&button_) == 1; }
    bool read_battery_mv(uint16_t& out_mv) { return battery_.read_mv(out_mv); }
    bool external_power() { return zephyr::Battery::external_power(); }
    bool charging() { return gpio_pin_get_dt(&charge_status_) == 1; }
    bool charged() { return gpio_pin_get_dt(&charge_done_) == 1; }

    ports::Capabilities capabilities() const {
        ports::Capabilities found = ports::Capability::None;
        if (gpio_is_ready_dt(&button_)) found |= ports::Capability::Contacts;
        if (pwm_is_ready_dt(&buzzer_)) found |= ports::Capability::Buzzer;
        if (device_is_ready(flash_device_)) found |= ports::Capability::Storage;
        if (battery_.ready()) found |= ports::Capability::Battery;
        if (die_temperature_.ready()) found |= ports::Capability::DieTemperature;
        if (indicator_.ready()) found |= ports::Capability::Indicator;
        return found;
    }

   private:
    static bool input(const struct gpio_dt_spec& pin, const char* name) {
        const int error = gpio_pin_configure_dt(&pin, GPIO_INPUT);
        if (error != 0) printk("meshtracker_x1: %s pin not configured (%d)\n", name, error);
        return error == 0;
    }

    const struct device* gpio0_{DEVICE_DT_GET(DT_NODELABEL(gpio0))};
    const struct device* gpio1_{DEVICE_DT_GET(DT_NODELABEL(gpio1))};
    const struct device* sensor_i2c_device_{DEVICE_DT_GET(DT_ALIAS(sensor_i2c))};
    const struct device* battery_device_{DEVICE_DT_GET(DT_NODELABEL(vbatt))};
    const struct device* flash_device_{DEVICE_DT_GET(DT_NODELABEL(ext_flash))};
    struct pwm_dt_spec buzzer_ = PWM_DT_SPEC_GET(DT_ALIAS(buzzer));
    struct gpio_dt_spec haptic_enable_ = GPIO_DT_SPEC_GET(DT_ALIAS(haptic_enable), gpios);
    struct gpio_dt_spec button_ = GPIO_DT_SPEC_GET(DT_ALIAS(button), gpios);
    struct gpio_dt_spec charge_status_ = GPIO_DT_SPEC_GET(DT_ALIAS(charge_status), gpios);
    struct gpio_dt_spec charge_done_ = GPIO_DT_SPEC_GET(DT_ALIAS(charge_done), gpios);
    struct gpio_dt_spec led_green_ = GPIO_DT_SPEC_GET(DT_ALIAS(led_green), gpios);
    struct gpio_dt_spec led_red_ = GPIO_DT_SPEC_GET(DT_ALIAS(led_red), gpios);
    struct gpio_dt_spec led_blue_ = GPIO_DT_SPEC_GET(DT_ALIAS(led_blue), gpios);

    zephyr::Clock clock_{};
    Gpio gpio_{gpio0_, gpio1_};
    Delay delay_{};
    I2c sensor_i2c_{sensor_i2c_device_};
    // INFO: fc 11nov26 P1.05 enables a DRV2605, so this motor stands in until the board attaches it
    PinMotor motor_{haptic_enable_};
    Annunciator annunciator_{buzzer_, motor_};
    Indicator indicator_{led_green_, led_red_, led_blue_};
    KeyValueStore key_value_{};
    zephyr::Battery battery_{battery_device_};
    zephyr::DieTemperature die_temperature_{};
    zephyr::Watchdog watchdog_{};
    bool storage_mounted_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
