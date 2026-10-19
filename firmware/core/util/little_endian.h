#ifndef SKYBLIP_CORE_UTIL_LITTLE_ENDIAN_H
#define SKYBLIP_CORE_UTIL_LITTLE_ENDIAN_H

#include <cstdint>

namespace skyblip {

inline void put_u16(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
}

inline void put_u24(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
    out[2] = static_cast<uint8_t>(value >> 16);
}

inline void put_u32(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
    out[2] = static_cast<uint8_t>(value >> 16);
    out[3] = static_cast<uint8_t>(value >> 24);
}

inline void put_i16(uint8_t* out, int16_t value) { put_u16(out, static_cast<uint16_t>(value)); }
inline void put_i32(uint8_t* out, int32_t value) { put_u32(out, static_cast<uint32_t>(value)); }

inline uint16_t get_u16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8));
}

inline uint32_t get_u24(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16);
}

inline uint32_t get_u32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

inline int16_t get_i16(const uint8_t* bytes) { return static_cast<int16_t>(get_u16(bytes)); }
inline int32_t get_i32(const uint8_t* bytes) { return static_cast<int32_t>(get_u32(bytes)); }

}

#endif
