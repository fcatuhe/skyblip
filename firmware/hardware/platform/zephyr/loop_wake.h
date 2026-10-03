#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_LOOP_WAKE_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_LOOP_WAKE_H
#if defined(__ZEPHYR__)

#include <zephyr/kernel.h>

#include <cstdint>

#include "runtime/tasks.h"

namespace skyblip::platform::zephyr {

class LoopWake {
   public:
    LoopWake() { k_sem_init(&woken_, 0, 1); }

    // INFO: fc 03oct26 limit 1 and the pass drains every ring: a burst of wakes is one more pass
    void wake() { k_sem_give(&woken_); }

    void rest_after(int64_t pass_began_ticks) {
        k_sem_take(&woken_, K_MSEC(runtime::kServiceStepMs));
        const int64_t earliest =
            pass_began_ticks +
            static_cast<int64_t>(k_ms_to_ticks_ceil64(runtime::kServicePassFloorMs));
        if (k_uptime_ticks() < earliest) k_sleep(K_TIMEOUT_ABS_TICKS(earliest));
    }

   private:
    struct k_sem woken_;
};

inline LoopWake g_loop_wake;

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
