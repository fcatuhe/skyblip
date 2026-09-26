#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_UPLOAD_GATE_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_UPLOAD_GATE_H
#if defined(__ZEPHYR__)

#include <zephyr/sys/atomic.h>

namespace skyblip::platform::zephyr {

// INFO: fc 26sep26 the one word the main loop and the MCUmgr work queue share, atomic so no lock
class UploadGate {
   public:
    static void publish(bool allowed) { atomic_set_bit_to(&word_, kAllowedBit, allowed); }
    static bool allowed() { return atomic_test_bit(&word_, kAllowedBit); }

    static void note_finished() { atomic_set_bit(&word_, kFinishedBit); }
    static void forget_finished() { atomic_clear_bit(&word_, kFinishedBit); }
    static bool finished() { return atomic_test_bit(&word_, kFinishedBit); }

   private:
    static constexpr int kAllowedBit = 0;
    static constexpr int kFinishedBit = 1;
    inline static atomic_t word_ = ATOMIC_INIT(0);
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
