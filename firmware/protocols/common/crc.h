#ifndef SKYBLIP_PROTOCOLS_COMMON_CRC_H
#define SKYBLIP_PROTOCOLS_COMMON_CRC_H

#include <cstddef>
#include <cstdint>

namespace skyblip::wire {

// INFO: fc 13nov26 ADS-L E.3.1 and Mode S parity share generator 0x1FFF409, shifted left by 7
constexpr uint32_t crc24_pass(uint32_t crc, uint8_t byte) {
    const uint32_t kPolynomial = 0xFFFA0480u;
    crc |= byte;
    for (uint8_t bit = 0; bit < 8; bit++) {
        if (crc & 0x80000000u) crc ^= kPolynomial;
        crc <<= 1;
    }
    return crc;
}

constexpr uint32_t crc24_check(const uint8_t* data, size_t bytes) {
    uint32_t crc = 0;
    for (size_t i = 0; i < bytes; i++) crc = crc24_pass(crc, data[i]);
    return crc >> 8;
}

constexpr uint32_t crc24_compute(const uint8_t* data, size_t bytes) {
    uint32_t crc = 0;
    for (size_t i = 0; i < bytes; i++) crc = crc24_pass(crc, data[i]);
    crc = crc24_pass(crc, 0);
    crc = crc24_pass(crc, 0);
    crc = crc24_pass(crc, 0);
    return crc >> 8;
}

constexpr uint16_t crc16_ccitt(const uint8_t* data, size_t bytes, uint16_t crc = 0) {
    for (size_t i = 0; i < bytes; i++) {
        crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

}

#endif
