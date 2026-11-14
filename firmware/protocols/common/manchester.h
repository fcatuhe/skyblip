#ifndef SKYBLIP_PROTOCOLS_COMMON_MANCHESTER_H
#define SKYBLIP_PROTOCOLS_COMMON_MANCHESTER_H

#include <cstddef>
#include <cstdint>

namespace skyblip::wire {

uint8_t manchester_encode_nibble(uint8_t nibble);

uint8_t manchester_decode_byte(uint8_t coded);

void manchester_encode(const uint8_t* data, size_t bytes, uint8_t* out);

size_t manchester_decode(const uint8_t* coded, size_t out_bytes, uint8_t* out, uint8_t* error_mask);

constexpr uint16_t manchester_chips(uint8_t byte) {
    uint16_t chips = 0;
    for (int bit = 7; bit >= 0; bit--) {
        chips = static_cast<uint16_t>(chips << 2);
        chips = static_cast<uint16_t>(chips | (((byte >> bit) & 1u) ? 0x1u : 0x2u));
    }
    return chips;
}

constexpr uint8_t kSyncWordBytes = 4;

size_t manchester_frame(uint32_t sync_word, const uint8_t* frame, uint8_t bytes, uint8_t* chips);

}

#endif
