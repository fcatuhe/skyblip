#include <zephyr/kernel.h>

#include "hardware/platform/zephyr/loop_wake.h"
#include "hardware/platform/zephyr/meshtracker_x1.h"
#include "products/skyblip_bravo/product.h"
#include "products/skyblip_bravo/services/diagnostics.h"
#include "runtime/tasks.h"

using namespace skyblip;

namespace {

using Bravo = bravo::Product<platform::zephyr::MeshtrackerX1>;

platform::zephyr::MeshtrackerX1 g_platform;
Bravo g_product{g_platform};
bravo::DiagnosticsCollector<Bravo> g_diagnostics;

}  // namespace

int main(void) {
    // INFO: fc 12nov26 before setup, so a refused boot cannot hang awake; SYSTEM OFF stops it
    (void)g_platform.watchdog().arm(runtime::kHardwareWatchdogCeilingMs);

    // INFO: fc 14nov26 a failed setup runs on with the parts that answered
    (void)g_product.setup();
    if (g_product.boot_path() == power::BootPath::SleepAgain) {
        while (!g_product.park_refusal(static_cast<uint32_t>(k_uptime_get()))) {
            g_platform.watchdog().feed();
            k_sleep(K_MSEC(runtime::kServicePassCeilingMs));
        }
        g_platform.system_power().system_off();
    }

    for (;;) {
        const int64_t began_ticks = k_uptime_ticks();
        const uint32_t now_ms = static_cast<uint32_t>(k_ticks_to_ms_floor64(began_ticks));
        g_product.run_pass(now_ms);
        g_diagnostics.step(g_product, now_ms);

        if (g_product.may_feed_watchdog(now_ms)) g_platform.watchdog().feed();

        if (g_product.ready_to_power_off()) g_platform.system_power().system_off();

        platform::zephyr::g_loop_wake.rest_after(began_ticks);
    }
    return 0;
}
