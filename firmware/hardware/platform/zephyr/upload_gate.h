#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_UPLOAD_GATE_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_UPLOAD_GATE_H
#if defined(__ZEPHYR__)

#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>

#include "ports/dfu.h"

namespace skyblip::platform::zephyr {

// INFO: fc 26sep26 the main loop and the MCUmgr work queue share it, so no read is ever torn
class UploadGate {
   public:
    static void publish(bool allowed) { atomic_set_bit_to(&word_, kAllowedBit, allowed); }
    static bool allowed() { return atomic_test_bit(&word_, kAllowedBit); }

    static void note_finished() { atomic_set_bit(&word_, kFinishedBit); }
    static void forget_finished() { atomic_clear_bit(&word_, kFinishedBit); }
    static bool finished() { return atomic_test_bit(&word_, kFinishedBit); }

    static void publish_approved(const ports::ImageVersion& version) {
        const k_spinlock_key_t key = k_spin_lock(&approved_lock_);
        approved_ = version;
        k_spin_unlock(&approved_lock_, key);
    }
    static ports::ImageVersion approved() {
        const k_spinlock_key_t key = k_spin_lock(&approved_lock_);
        const ports::ImageVersion version = approved_;
        k_spin_unlock(&approved_lock_, key);
        return version;
    }

   private:
    static constexpr int kAllowedBit = 0;
    static constexpr int kFinishedBit = 1;
    inline static atomic_t word_ = ATOMIC_INIT(0);
    inline static k_spinlock approved_lock_{};
    inline static ports::ImageVersion approved_{};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
