// The RF channel reduced to an injectable scalar: a bit error rate, a burst
// length, or a symbol-error count applied to a frame. Test fixture, not firmware:
// it lets a protocol test measure detected-vs-silent decode failure with no PHY.
#ifndef SKYBLIP_PROTOCOLS_COMMON_TEST_CHANNEL_H
#define SKYBLIP_PROTOCOLS_COMMON_TEST_CHANNEL_H

#include <cstddef>
#include <cstdint>

namespace skyblip::wire {

// INFO: fc 13nov26 a deterministic xorshift, so a Monte-Carlo case fails the same way twice
class Random {
   public:
    explicit Random(uint32_t seed = 1) : state_(seed) {}

    uint32_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }
    double uniform() { return (next() >> 8) * (1.0 / 16777216.0); }

   private:
    uint32_t state_;
};

class RfChannel : public Random {
   public:
    explicit RfChannel(uint32_t seed = 1) : Random(seed) {}

    int apply_bit_error_rate(uint8_t* data, size_t bytes, double bit_error_rate,
                             uint8_t* error_mask = nullptr) {
        int flips = 0;
        for (size_t i = 0; i < bytes; i++) {
            for (int bit = 0; bit < 8; bit++) {
                if (uniform() < bit_error_rate) {
                    uint8_t mask = static_cast<uint8_t>(0x80 >> bit);
                    data[i] ^= mask;
                    if (error_mask) error_mask[i] |= mask;
                    flips++;
                }
            }
        }
        return flips;
    }

    int apply_burst(uint8_t* data, size_t bytes, int burst_bits, uint8_t* error_mask = nullptr) {
        size_t total_bits = bytes * 8;
        if (total_bits == 0) return 0;
        size_t start = next() % total_bits;
        int flips = 0;
        for (int offset = 0; offset < burst_bits; offset++) {
            size_t bit = (start + static_cast<size_t>(offset)) % total_bits;
            size_t byte = bit >> 3;
            uint8_t mask = static_cast<uint8_t>(0x80 >> (bit & 7));
            data[byte] ^= mask;
            if (error_mask) error_mask[byte] |= mask;
            flips++;
        }
        return flips;
    }

    int apply_symbol_errors(uint8_t* data, size_t bytes, int symbol_errors) {
        if (bytes == 0) return 0;
        int applied = 0;
        for (int i = 0; i < symbol_errors; i++) {
            size_t byte_index = next() % bytes;
            uint8_t flip;
            do {
                flip = static_cast<uint8_t>(next());
            } while (flip == 0);
            data[byte_index] ^= flip;
            applied++;
        }
        return applied;
    }
};

}

#endif
