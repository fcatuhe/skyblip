#include "core/fec/crc.h"

namespace skyblip::fec {

uint32_t adsl_crc_pass(uint32_t crc, uint8_t byte) {
    const uint32_t kPolynomial = 0xFFFA0480u;
    crc |= byte;
    for (uint8_t bit = 0; bit < 8; bit++) {
        if (crc & 0x80000000u) crc ^= kPolynomial;
        crc <<= 1;
    }
    return crc;
}

uint32_t adsl_crc_check(const uint8_t* data, size_t bytes) {
    uint32_t crc = 0;
    for (size_t i = 0; i < bytes; i++) crc = adsl_crc_pass(crc, data[i]);
    return crc >> 8;
}

uint32_t adsl_crc_compute(const uint8_t* data, size_t bytes) {
    uint32_t crc = 0;
    for (size_t i = 0; i < bytes; i++) crc = adsl_crc_pass(crc, data[i]);
    crc = adsl_crc_pass(crc, 0);
    crc = adsl_crc_pass(crc, 0);
    crc = adsl_crc_pass(crc, 0);
    return crc >> 8;
}

uint16_t crc16_ccitt(const uint8_t* data, size_t bytes, uint16_t crc) {
    for (size_t i = 0; i < bytes; i++) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

uint32_t crc32(const uint8_t* data, size_t bytes, uint32_t crc) {
    for (size_t i = 0; i < bytes; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

}
