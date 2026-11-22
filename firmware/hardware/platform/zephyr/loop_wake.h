#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_LOOP_WAKE_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_LOOP_WAKE_H
#if defined(__ZEPHYR__)

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <cstdint>

#include "core/util/result.h"

namespace skyblip::platform::zephyr {

class LoopWake {
   public:
    LoopWake() { k_sem_init(&woken_, 0, 1); }

    // INFO: fc 03oct26 limit 1 and the pass drains every ring: a burst of wakes is one more pass
    void wake() { k_sem_give(&woken_); }

    void rest_after(int64_t pass_began_ticks, uint32_t due_in_ms, uint32_t floor_ms) {
        const int64_t due =
            pass_began_ticks + static_cast<int64_t>(k_ms_to_ticks_ceil64(due_in_ms));
        (void)k_sem_take(&woken_, K_TIMEOUT_ABS_TICKS(due));
        const int64_t earliest =
            pass_began_ticks + static_cast<int64_t>(k_ms_to_ticks_ceil64(floor_ms));
        if (k_uptime_ticks() < earliest) k_sleep(K_TIMEOUT_ABS_TICKS(earliest));
    }

   private:
    struct k_sem woken_;
};

inline LoopWake g_loop_wake;

// INFO: fc 19nov26 a contact's edges wake the loop, which debounces it on its own clock
inline Status wake_on_edges(const struct gpio_dt_spec& pin, struct gpio_callback& callback) {
    gpio_init_callback(
        &callback,
        [](const struct device*, struct gpio_callback*, gpio_port_pins_t) { g_loop_wake.wake(); },
        BIT(pin.pin));
    if (gpio_add_callback(pin.port, &callback) != 0) return Status::Down;
    return gpio_pin_interrupt_configure_dt(&pin, GPIO_INT_EDGE_BOTH) == 0 ? Status::Ok
                                                                          : Status::Down;
}

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
