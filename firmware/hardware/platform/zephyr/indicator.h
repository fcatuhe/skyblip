#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_INDICATOR_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_INDICATOR_H
#if defined(__ZEPHYR__)

#include <zephyr/drivers/gpio.h>

#include "ports/indicator.h"

namespace skyblip::platform::zephyr {

class Indicator : public ports::Indicator {
   public:
    Indicator(const struct gpio_dt_spec& green, const struct gpio_dt_spec& red,
              const struct gpio_dt_spec& blue)
        : green_(green), red_(red), blue_(blue) {}

    bool ready() const {
        return gpio_is_ready_dt(&green_) && gpio_is_ready_dt(&red_) && gpio_is_ready_dt(&blue_);
    }

    void begin() {
        if (ready()) show(indication::Colour::None);
    }

    void show(indication::Colour colour) override {
        drive(green_, colour == indication::Colour::Green);
        drive(red_, colour == indication::Colour::Red);
        drive(blue_, colour == indication::Colour::Blue);
    }

    // INFO: fc 20nov26 active-low LEDs held high would feed the collapsed rail, so park()
    // disconnects them: hardware/README.md
    void park() override {
        show(indication::Colour::None);
        release(green_);
        release(red_);
        release(blue_);
    }

   private:
    // INFO: fc 20nov26 reconfigured on every call, which costs the same as set and makes park()
    // reversible
    static void drive(const struct gpio_dt_spec& pin, bool on) {
        if (pin.port == nullptr) return;
        gpio_pin_configure_dt(&pin, on ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
    }

    static void release(const struct gpio_dt_spec& pin) {
        if (pin.port == nullptr) return;
        gpio_pin_configure_dt(&pin, GPIO_DISCONNECTED);
    }

    struct gpio_dt_spec green_;
    struct gpio_dt_spec red_;
    struct gpio_dt_spec blue_;
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
