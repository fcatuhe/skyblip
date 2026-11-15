#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RANDOM_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RANDOM_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/entropy.h>

#include <cstdint>

#include "ports/random.h"

BUILD_ASSERT(IS_ENABLED(CONFIG_ENTROPY_NRF5_BIAS_CORRECTION),
             "nonces and keys are drawn from the RNG, which is biased without its correction");
BUILD_ASSERT(
    DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_entropy), nordic_nrf_rng),
    "nonces and keys are drawn from the nRF RNG, the source the bias correction applies to");

namespace skyblip::platform::zephyr {

class Random : public ports::Random {
   public:
    bool draw(ByteSpan out) override {
        return device_is_ready(entropy_) &&
               entropy_get_entropy(entropy_, out.data(), static_cast<uint16_t>(out.size())) == 0;
    }

   private:
    const struct device* entropy_{DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy))};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
