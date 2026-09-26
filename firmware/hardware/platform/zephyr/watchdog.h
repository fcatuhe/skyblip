#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_WATCHDOG_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_WATCHDOG_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/task_wdt/task_wdt.h>

#include "ports/watchdog.h"
#include "runtime/tasks.h"

namespace skyblip::platform::zephyr {

// Zephyr's task watchdog: one software channel fed by the service loop, with the
// nRF52's WDT peripheral installed underneath it as the hardware fallback. The
// subsystem sizes that fallback from CONFIG_TASK_WDT_MIN_TIMEOUT plus
// CONFIG_TASK_WDT_HW_FALLBACK_DELAY, which prj.conf sets above this channel's
// period so the channel is always the first thing to expire.
class Watchdog : public ports::Watchdog {
   public:
    static constexpr uint32_t kRopeMs =
        CONFIG_TASK_WDT_MIN_TIMEOUT + CONFIG_TASK_WDT_HW_FALLBACK_DELAY;
    // INFO: fc 23sep26 under MIN_TIMEOUT, so task_wdt's background feed never renews the rope
    static_assert(CONFIG_TASK_WDT_MIN_TIMEOUT > 1000, "kChannelMs would wrap below zero");
    static constexpr uint32_t kChannelMs = CONFIG_TASK_WDT_MIN_TIMEOUT - 1000;
    static_assert(kRopeMs == runtime::kHardwareWatchdogMs,
                  "prj.conf's task_wdt window must be the rope the loop is supervised by");

    Status arm(uint32_t timeout_ms) override {
        if (armed_) return Status::Ok;
        if (timeout_ms != kRopeMs) return Status::Invalid;
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(wdt0))
        const struct device* hardware = DEVICE_DT_GET(DT_NODELABEL(wdt0));
        if (!device_is_ready(hardware)) return Status::Down;
#else
        const struct device* hardware = nullptr;
#endif
        if (task_wdt_init(hardware) != 0) return Status::Down;
        channel_ = task_wdt_add(kChannelMs, &Watchdog::bite, nullptr);
        if (channel_ < 0) return Status::Down;
        armed_ = true;
        return Status::Ok;
    }

    void feed() override {
        if (armed_) task_wdt_feed(channel_);
    }

    bool armed() const override { return armed_; }

   private:
    // INFO: hk 02aug26 the channel callback deliberately does NOT call
    // sys_reboot(): a software reset lands in RESETREAS as SREQ, which is
    // indistinguishable from a requested restart. Spinning here starves the
    // hardware fallback too, so the reset that follows is a real WDT bite and
    // the next boot reports DOG. SoftRF uses the same deliberate bite as its
    // reboot primitive because on the nRF52 the watchdog cannot be stopped once
    // it is started (src/platform/nRF52.cpp:3240-3292).
    static void bite(int channel_id, void* user_data) {
        ARG_UNUSED(channel_id);
        ARG_UNUSED(user_data);
        for (;;) k_busy_wait(1000);
    }

    int channel_{-1};
    bool armed_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
