#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_SUPPLY_MONITOR_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_SUPPLY_MONITOR_H
#if defined(__ZEPHYR__)

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "hardware/platform/zephyr/loop_wake.h"

#if defined(CONFIG_NRFX_POWER)
#include <hal/nrf_power.h>
#include <nrfx_power.h>
#endif

namespace skyblip::platform::zephyr {

// INFO: fc 15nov26 Zephyr has no POFCON driver, so this arms it through nrfx: hardware/README.md
class SupplyMonitor {
   public:
    // INFO: fc 05aug26 2.8 V, the highest POFCON offers for VDD, because a lower warning arrives
    // after the write: hardware/README.md
#if defined(CONFIG_NRFX_POWER)
    static constexpr nrf_power_pof_thr_t kThreshold = NRF_POWER_POFTHR_V28;
#endif

    void arm() {
#if defined(CONFIG_NRFX_POWER)
        if (!armed_) {
            nrfx_power_pofwarn_config_t config = {};
            config.handler = on_warning;
            config.thr = kThreshold;
#if NRFX_POWER_SUPPORTS_POFCON_VDDH
            // INFO: fc 20nov26 unused in normal-voltage mode, so it gets the register's reset
            // value, not a brace-initialised zero
            config.thrvddh = NRF_POWER_POFTHRVDDH_V27;
#endif
            nrfx_power_pof_init(&config);
            nrfx_power_pof_enable(&config);
        }
        // INFO: fc 20nov26 armed_ is what POFCON holds, read back, so a failed arm is never
        // reported armed
        bool enabled = false;
        (void)nrf_power_pofcon_get(NRF_POWER, &enabled);
        armed_ = enabled;
#endif
    }

    bool armed() const { return armed_; }

    // INFO: fc 20nov26 the interrupt writes the counter and a service pass clears it, so both sides
    // stay atomic
    bool take_warning() { return atomic_clear(&warnings_) != 0; }

   private:
    // INFO: fc 20nov26 interrupt context: one atomic increment and a loop wake, and core/power
    // decides the rest
    static void on_warning() {
        atomic_inc(&warnings_);
        g_loop_wake.wake();
    }

    static inline atomic_t warnings_ = 0;
    bool armed_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
