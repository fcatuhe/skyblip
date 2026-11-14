#include <zephyr/kernel.h>

#include "hardware/platform/zephyr/loop_wake.h"
#include "hardware/platform/zephyr/t_echo_plus.h"
#include "products/skyblip_go/product.h"
#include "products/skyblip_go/services/diagnostics.h"
#include "runtime/tasks.h"

using namespace skyblip;

namespace {

using Go = go::Product<platform::zephyr::TEchoPlus>;

platform::zephyr::TEchoPlus g_platform;
Go g_product{g_platform};

// Reads what the services and the board already count into the snapshot the
// `diagnostics` command answers from. Not a service: it consumes the product
// rather than being stepped by it.
go::DiagnosticsCollector<Go> g_diagnostics;

}  // namespace

int main(void) {
    // INFO: fc 05oct26 before setup, so a refused boot cannot hang awake; SYSTEM OFF stops it
    (void)g_platform.watchdog().arm(runtime::kHardwareWatchdogCeilingMs);

    // INFO: fc 14nov26 a failed setup runs on with the parts that answered
    (void)g_product.setup();
    // INFO: fc 21sep26 the refusal performed: no service, at most one frame, and it never returns
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
