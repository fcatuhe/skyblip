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
    // INFO: fc 05aug26 2.8 V, the highest threshold POFCON offers for VDD
    // (nRF52840 PS v1.8 §5.1.3 POFCON.THRESHOLD, V17..V28). Highest on purpose:
    // the earlier the warning the more of it there is, and everything above this
    // voltage is already the divider's business - our own cutoff is a 3.2 V cell
    // and this rail is at or below the cell. What it must not do is fire on a
    // healthy pack, so what the bench has to confirm is that a 14 dBm burst on a
    // 3.4 V cell leaves VDD above 2.8 V; if it does not, the answer is the
    // decoupling, not a lower threshold, because a threshold under 2.8 V is a
    // warning that arrives after the write it was meant to prevent.
    //
    // Whether VDD here is the cell or a regulated 3.3 V rail is a schematic fact
    // this repository does not hold (project/reference/ has no T-Echo Plus
    // schematic), and the choice above is the same either way: 2.8 V is below any
    // healthy cell and above the SoC's own brownout floor (1.7 V).
#if defined(CONFIG_NRFX_POWER)
    static constexpr nrf_power_pof_thr_t kThreshold = NRF_POWER_POFTHR_V28;
#endif

    // Once, at boot. Idempotent: a second call re-reads the register rather than
    // writing it again.
    void arm() {
#if defined(CONFIG_NRFX_POWER)
        if (!armed_) {
            nrfx_power_pofwarn_config_t config = {};
            config.handler = on_warning;
            config.thr = kThreshold;
#if NRFX_POWER_SUPPORTS_POFCON_VDDH
            // The board runs in normal-voltage mode, so this comparator is not the
            // one that can fire; it is set to the register's own reset value
            // rather than to the zero a brace-initialised struct would write.
            config.thrvddh = NRF_POWER_POFTHRVDDH_V27;
#endif
            nrfx_power_pof_init(&config);
            nrfx_power_pof_enable(&config);
        }
        // The proof, not the intent: what POFCON actually holds now.
        bool enabled = false;
        (void)nrf_power_pofcon_get(NRF_POWER, &enabled);
        armed_ = enabled;
#endif
    }

    bool armed() const { return armed_; }

    // Read and cleared. Called from a service pass; the counter it reads is
    // written in interrupt context, which is the whole reason it is atomic.
    bool take_warning() { return atomic_clear(&warnings_) != 0; }

   private:
    // Interrupt context, on the shared POWER/CLOCK line. It does one atomic
    // increment and wakes the loop: the decision is core/power's, and the pass it
    // wakes is the only budget a collapsing rail gives.
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
