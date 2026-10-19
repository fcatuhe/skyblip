// The RF channel reduced to an injectable scalar: a bit error rate, a burst
// length, or a symbol-error count applied to a frame. Test fixture, not firmware:
// it lets a protocol test measure detected-vs-silent decode failure with no PHY.
#ifndef SKYBLIP_TEST_SUPPORT_RF_CHANNEL_H
#define SKYBLIP_TEST_SUPPORT_RF_CHANNEL_H

#include <cstddef>
#include <cstdint>

#include "test/support/random.h"

namespace skyblip {

class RfChannel : public Random {
   public:
    explicit RfChannel(uint32_t seed = 1) : Random(seed) {}

    int apply_bit_error_rate(uint8_t* data, size_t length, double bit_error_rate,
                             uint8_t* error_mask = nullptr);

    int apply_burst(uint8_t* data, size_t length, int burst_bits, uint8_t* error_mask = nullptr);

    int apply_symbol_errors(uint8_t* data, size_t length, int symbol_errors);
};

}  // namespace skyblip

#endif
