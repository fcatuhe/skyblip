#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_UPLOAD_GATE_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_UPLOAD_GATE_H
#if defined(__ZEPHYR__)

#include <zephyr/sys/atomic.h>

namespace skyblip::platform::zephyr {

// INFO: fc 26sep26 the main loop publishes, the MCUmgr work queue reads: one atomic word, no lock
class UploadGate {
   public:
    static void publish(bool allowed) { atomic_set_bit_to(&word_, kAllowedBit, allowed); }
    static bool allowed() { return atomic_test_bit(&word_, kAllowedBit); }

   private:
    static constexpr int kAllowedBit = 0;
    inline static atomic_t word_ = ATOMIC_INIT(0);
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
