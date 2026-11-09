#include "core/fec/manchester.h"

#include <array>

namespace skyblip::fec {

namespace {

// INFO: fc 09nov26 C.2.1: a 1 goes out as the chips 01 and a 0 as 10, first chip first
constexpr std::array<uint8_t, 16> encode_table() {
    std::array<uint8_t, 16> table{};
    for (int nibble = 0; nibble < 16; nibble++) {
        int coded = 0;
        for (int bit = 3; bit >= 0; bit--)
            coded = (coded << 2) | (((nibble >> bit) & 1) ? 0b01 : 0b10);
        table[nibble] = static_cast<uint8_t>(coded);
    }
    return table;
}

// INFO: fc 09nov26 a pair of equal chips is neither symbol, so its bit is marked in the high nibble
constexpr std::array<uint8_t, 256> decode_table() {
    std::array<uint8_t, 256> table{};
    for (int coded = 0; coded < 256; coded++) {
        int nibble = 0;
        int doubtful = 0;
        for (int pair = 0; pair < 4; pair++) {
            const int chips = (coded >> (2 * pair)) & 0b11;
            nibble |= (chips & 1) << pair;
            if (chips == 0b00 || chips == 0b11) doubtful |= 1 << pair;
        }
        table[coded] = static_cast<uint8_t>((doubtful << 4) | nibble);
    }
    return table;
}

constexpr std::array<uint8_t, 16> kEncode = encode_table();
constexpr std::array<uint8_t, 256> kDecode = decode_table();

}

uint8_t manchester_encode_nibble(uint8_t nibble) { return kEncode[nibble & 0x0F]; }

uint8_t manchester_decode_byte(uint8_t coded) { return kDecode[coded]; }

void manchester_encode(const uint8_t* data, size_t bytes, uint8_t* out) {
    for (size_t i = 0; i < bytes; i++) {
        out[2 * i] = kEncode[data[i] >> 4];
        out[2 * i + 1] = kEncode[data[i] & 0x0F];
    }
}

size_t manchester_decode(const uint8_t* coded, size_t out_bytes, uint8_t* out,
                         uint8_t* error_mask) {
    size_t bad_bits = 0;
    for (size_t i = 0; i < out_bytes; i++) {
        uint8_t high = kDecode[coded[2 * i]];
        uint8_t low = kDecode[coded[2 * i + 1]];
        out[i] = static_cast<uint8_t>(((high & 0x0F) << 4) | (low & 0x0F));
        uint8_t errors = static_cast<uint8_t>((high & 0xF0) | ((low & 0xF0) >> 4));
        if (error_mask) error_mask[i] = errors;
        bad_bits += __builtin_popcount(errors);
    }
    return bad_bits;
}

}
