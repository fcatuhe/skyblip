#include "core/util/crc32.h"

namespace skyblip {

namespace {

constexpr uint32_t kPolynomial = 0xEDB88320u;

constexpr uint32_t nibble_step(uint32_t crc) {
    for (int bit = 0; bit < 4; bit++) crc = (crc & 1) ? (crc >> 1) ^ kPolynomial : (crc >> 1);
    return crc;
}

constexpr uint32_t kNibbles[16] = {
    nibble_step(0),  nibble_step(1),  nibble_step(2),  nibble_step(3),
    nibble_step(4),  nibble_step(5),  nibble_step(6),  nibble_step(7),
    nibble_step(8),  nibble_step(9),  nibble_step(10), nibble_step(11),
    nibble_step(12), nibble_step(13), nibble_step(14), nibble_step(15)};

}  // namespace

uint32_t crc32(const uint8_t* data, size_t bytes, uint32_t crc) {
    for (size_t i = 0; i < bytes; i++) {
        crc ^= data[i];
        crc = (crc >> 4) ^ kNibbles[crc & 0x0F];
        crc = (crc >> 4) ^ kNibbles[crc & 0x0F];
    }
    return crc ^ 0xFFFFFFFFu;
}

}  // namespace skyblip
