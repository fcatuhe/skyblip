// Harness, not a test: the impairment the protocol suites run their frames
// through. Bit errors and burst errors are injected here so the codecs can be
// measured against a channel that misbehaves on demand, deterministically, with no
// radio in the room. Nothing below the services is mocked, so this models the air
// rather than the parts.
#include "test/support/rf_channel.h"

namespace skyblip {

int RfChannel::apply_bit_error_rate(uint8_t* data, size_t bytes, double bit_error_rate,
                                    uint8_t* error_mask) {
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

int RfChannel::apply_burst(uint8_t* data, size_t bytes, int burst_bits, uint8_t* error_mask) {
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

int RfChannel::apply_symbol_errors(uint8_t* data, size_t bytes, int symbol_errors) {
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

}  // namespace skyblip
