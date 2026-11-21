#ifndef SKYBLIP_PRODUCTS_COMMON_MAIN_LOOP_H
#define SKYBLIP_PRODUCTS_COMMON_MAIN_LOOP_H

#include <zephyr/kernel.h>

#include <type_traits>

#include "core/power/wake.h"
#include "hardware/platform/zephyr/loop_wake.h"
#include "runtime/tasks.h"

namespace skyblip::common {

template <class Platform, class Product, class Collector>
[[noreturn]] void run(Platform& platform, Product& product, Collector& collector) {
    static_assert(std::remove_reference_t<decltype(platform.watchdog())>::kChannelPeriodMs >
                      runtime::kServiceSilenceCeilingMs,
                  "the loop refuses to feed before the watchdog bites, so a reset names the stall");
    // INFO: fc 05oct26 before setup, so a refused boot cannot hang awake; SYSTEM OFF stops it
    (void)platform.watchdog().arm();

    // INFO: fc 14nov26 a failed setup runs on with the parts that answered
    (void)product.setup();
    // INFO: fc 21sep26 the refusal performed: no service, at most one frame, and it never returns
    if (product.boot_path() == power::BootPath::SleepAgain) {
        while (!product.park_refusal(static_cast<uint32_t>(k_uptime_get()))) {
            platform.watchdog().feed();
            k_sleep(K_MSEC(runtime::kServicePassCeilingMs));
        }
        platform.system_power().system_off();
    }

    for (;;) {
        const int64_t began_ticks = k_uptime_ticks();
        const uint32_t now_ms = static_cast<uint32_t>(k_ticks_to_ms_floor64(began_ticks));
        product.run_pass(now_ms);
        collector.step(product, now_ms);

        if (product.may_feed_watchdog(now_ms)) platform.watchdog().feed();

        if (product.ready_to_power_off()) platform.system_power().system_off();

        skyblip::platform::zephyr::g_loop_wake.rest_after(began_ticks, product.due_in_ms(now_ms));
    }
}

}  // namespace skyblip::common

#endif
