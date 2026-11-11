#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_AG3335_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_AG3335_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include "core/util/result.h"
#include "hardware/platform/zephyr/io.h"
#include "hardware/platform/zephyr/pps.h"

namespace skyblip::platform::zephyr {

static_assert(IS_ENABLED(CONFIG_UART_INTERRUPT_DRIVEN),
              "the AG3335's UART is read from its receive interrupt");

class Ag3335Wiring {
   public:
    // TODO: fc 11nov26 SoftRF's order and levels, nRF52.cpp:1865-1878, unverified until #169
    Status begin() {
        if (!lines_ready()) return Status::Down;
        if (gpio_pin_configure_dt(&enable_, GPIO_OUTPUT_ACTIVE) != 0) return Status::Down;
        if (gpio_pin_configure_dt(&rtc_enable_, GPIO_OUTPUT_ACTIVE) != 0) return Status::Down;
        if (gpio_pin_configure_dt(&reset_, GPIO_OUTPUT_INACTIVE) != 0) return Status::Down;
        if (gpio_pin_configure_dt(&sleep_, GPIO_OUTPUT_ACTIVE) != 0) return Status::Down;
        if (gpio_pin_configure_dt(&rtc_wake_, GPIO_OUTPUT_INACTIVE) != 0) return Status::Down;
        if (!device_is_ready(uart_device_)) return Status::Down;
        if (uart_.begin() != Status::Ok) return Status::Down;
        pps_armed_ = pps_.begin() == Status::Ok;
        return Status::Ok;
    }

    io::Uart& uart() { return uart_; }
    zephyr::Pps& pps() { return pps_; }
    bool pps_armed() const { return pps_armed_; }

   private:
    bool lines_ready() const {
        return gpio_is_ready_dt(&enable_) && gpio_is_ready_dt(&rtc_enable_) &&
               gpio_is_ready_dt(&reset_) && gpio_is_ready_dt(&sleep_) &&
               gpio_is_ready_dt(&rtc_wake_);
    }

    const struct device* uart_device_{DEVICE_DT_GET(DT_ALIAS(gnss_uart))};
    struct gpio_dt_spec enable_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_enable), gpios);
    struct gpio_dt_spec rtc_enable_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_rtc_enable), gpios);
    struct gpio_dt_spec reset_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_reset), gpios);
    struct gpio_dt_spec sleep_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_sleep), gpios);
    struct gpio_dt_spec rtc_wake_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_rtc_wake), gpios);
    Uart uart_{uart_device_};
    Pps pps_{};
    bool pps_armed_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
