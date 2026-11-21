#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_WATCHDOG_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_WATCHDOG_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/task_wdt/task_wdt.h>

#include "core/util/result.h"

namespace skyblip::platform::zephyr {

// INFO: fc 20nov26 prj.conf sets the hardware fallback above this channel's period, so the channel
// always expires first
class Watchdog {
   public:
    // INFO: fc 26sep26 under MIN_TIMEOUT, so task_wdt's background feed never renews the rope
    static_assert(CONFIG_TASK_WDT_MIN_TIMEOUT > 1000, "kChannelPeriodMs would wrap below zero");
    static constexpr uint32_t kChannelPeriodMs = CONFIG_TASK_WDT_MIN_TIMEOUT - 1000;

    // INFO: hk 02aug26 no stop(): a running nRF52 watchdog cannot stop (nRF52840 PS v1.8 §6.34)
    Status arm() {
        if (armed_) return Status::Ok;
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(wdt0))
        const struct device* hardware = DEVICE_DT_GET(DT_NODELABEL(wdt0));
        if (!device_is_ready(hardware)) return Status::Down;
#else
        const struct device* hardware = nullptr;
#endif
        if (task_wdt_init(hardware) != 0) return Status::Down;
        channel_ = task_wdt_add(kChannelPeriodMs, &Watchdog::bite, nullptr);
        if (channel_ < 0) return Status::Down;
        armed_ = true;
        return Status::Ok;
    }

    void feed() {
        if (armed_) task_wdt_feed(channel_);
    }

    bool armed() const { return armed_; }

   private:
    // INFO: hk 02aug26 it spins rather than call sys_reboot(), so the reset is a real WDT bite the
    // next boot reports as DOG
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
