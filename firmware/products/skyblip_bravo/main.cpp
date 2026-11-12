#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "core/util/hex.h"
#include "hardware/platform/zephyr/console.h"
#include "hardware/platform/zephyr/loop_wake.h"
#include "hardware/platform/zephyr/meshtracker_x1.h"
#include "products/skyblip_bravo/product.h"
#include "products/skyblip_bravo/services/diagnostics.h"
#include "runtime/tasks.h"

LOG_MODULE_REGISTER(skyblip, CONFIG_SKYBLIP_BRAVO_LOG_LEVEL);

using namespace skyblip;

namespace {

using Bravo = bravo::Product<platform::zephyr::MeshtrackerX1>;

platform::zephyr::MeshtrackerX1 g_platform;
Bravo g_product{g_platform};
bravo::DiagnosticsDump<Bravo, platform::zephyr::Console> g_diagnostics;

// INFO: fc 12nov26 the inventory #170 moved here: what answered, for the bench's first boot
void log_inventory() {
    const ports::Inventory& found = g_product.board().inventory();
    LOG_INF("inventory: barometer 0x%02x, haptic %d, rtc %d, %u on the sensor bus",
            found.barometer_address, static_cast<int>(found.haptic), found.rtc_fitted,
            found.i2c_count);
    for (uint8_t i = 0; i < found.i2c_count; i++)
        LOG_INF("inventory: sensor bus 0x%02x answered", found.i2c_addresses[i]);
}

// INFO: fc 12nov26 scripts/adsb_rejects.py parses this line, so keep its keys and their order
void log_adsb_rejects() {
    bravo::AdsbReject reject{};
    while (g_product.adsb().take_reject(reject)) {
        char chips[2 * protocol::kAdsbChipBytes + 1];
        chips[hex::write(ConstByteSpan(reject.chips.data(), reject.chips.size()), chips)] = 0;
        const char* status = reject.status == Status::Unsupported ? "unsupported" : "crc";
        if (reject.rssi_valid) {
            LOG_DBG("adsb reject status=%s rssi=%d chips=%s", status,
                    static_cast<int>(reject.rssi_dbm), chips);
        } else {
            LOG_DBG("adsb reject status=%s rssi=none chips=%s", status, chips);
        }
    }
}

}  // namespace

int main(void) {
    // INFO: fc 12nov26 before setup, so a refused boot cannot hang awake; SYSTEM OFF stops it
    if (g_platform.watchdog().arm(runtime::kHardwareWatchdogCeilingMs) != Status::Ok)
        LOG_ERR("watchdog: not armed, the loop is unsupervised");

    const Status started = g_product.setup();
    LOG_INF("reset reason: %s, setup done at %u ms", power::to_string(g_product.reset_reason()),
            static_cast<unsigned>(k_uptime_get()));

    if (g_product.boot_path() == power::BootPath::SleepAgain) {
        const power::BootCell& cell = g_product.boot_cell();
        LOG_INF("back to sleep at %u mV (lockout %u mV, external power %d)", cell.cell_mv,
                power::kBootLockoutMv, static_cast<int>(cell.external_power));
        while (!g_product.park_refusal(static_cast<uint32_t>(k_uptime_get()))) {
            g_platform.watchdog().feed();
            k_sleep(K_MSEC(runtime::kServicePassCeilingMs));
        }
        g_platform.system_power().system_off();
    }

    log_inventory();
    if (!g_platform.gnss_wired()) LOG_ERR("GNSS: its lines did not come up, no fix will arrive");
    if (!g_platform.pps_armed()) LOG_ERR("PPS: no edge interrupt, fixes are dated on arrival");
    if (!g_platform.storage_mounted())
        LOG_ERR("NVS: did not mount, settings live in RAM until the next boot");
    if (!g_platform.link_up()) LOG_ERR("bluetooth: did not come up, no phone can connect");
    if (!g_platform.system_power().supply_monitor_armed())
        LOG_ERR("POFCON: not armed, a collapsing rail can land inside a flash write");

    if (started == Status::Ok) {
        LOG_INF("skyBlip Bravo up: capabilities=%u degraded=%u",
                static_cast<unsigned>(g_product.capabilities()),
                static_cast<unsigned>(g_product.degraded()));
    } else {
        LOG_ERR("self test %s: missing=%u", to_string(started),
                static_cast<unsigned>(ports::without(bravo::kRequired, g_product.capabilities())));
    }

    bool reported_stall = false;
    for (;;) {
        const int64_t began_ticks = k_uptime_ticks();
        const uint32_t now_ms = static_cast<uint32_t>(k_ticks_to_ms_floor64(began_ticks));
        g_product.run_pass(now_ms);
        g_diagnostics.step(g_product, now_ms);
        log_adsb_rejects();

        if (g_product.may_feed_watchdog(now_ms)) {
            g_platform.watchdog().feed();
        } else if (!reported_stall) {
            reported_stall = true;
            const int stalled = g_product.stalled_service(now_ms);
            LOG_ERR("watchdog: service '%s' stopped reporting progress; letting it bite",
                    g_product.service_name(stalled));
        }

        if (g_product.ready_to_power_off()) {
            LOG_INF("power off: %s", power::to_string(g_product.shutdown().reason()));
            g_platform.system_power().system_off();
        }

        platform::zephyr::g_loop_wake.rest_after(began_ticks);
    }
    return 0;
}
