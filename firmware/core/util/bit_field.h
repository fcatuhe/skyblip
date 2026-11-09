#ifndef SKYBLIP_CORE_UTIL_BIT_FIELD_H
#define SKYBLIP_CORE_UTIL_BIT_FIELD_H

#include <cstdint>

namespace skyblip {

// INFO: fc 09nov26 bits count from bit 0 of byte 0 upwards, the order ADS-L B.3 numbers them
struct BitField {
    uint16_t offset;
    uint8_t width;
};

inline uint32_t read_bits(const uint8_t* bytes, BitField field) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < field.width; i++) {
        const uint16_t bit = static_cast<uint16_t>(field.offset + i);
        if (bytes[bit >> 3] & (1u << (bit & 7))) value |= 1u << i;
    }
    return value;
}

inline int32_t read_signed_bits(const uint8_t* bytes, BitField field) {
    const uint32_t value = read_bits(bytes, field);
    const uint32_t sign = 1u << (field.width - 1);
    return static_cast<int32_t>(value ^ sign) - static_cast<int32_t>(sign);
}

inline void write_bits(uint8_t* bytes, BitField field, uint32_t value) {
    for (uint8_t i = 0; i < field.width; i++) {
        const uint16_t bit = static_cast<uint16_t>(field.offset + i);
        const uint8_t mask = static_cast<uint8_t>(1u << (bit & 7));
        if (value & (1u << i))
            bytes[bit >> 3] = static_cast<uint8_t>(bytes[bit >> 3] | mask);
        else
            bytes[bit >> 3] = static_cast<uint8_t>(bytes[bit >> 3] & ~mask);
    }
}

}

#endif
