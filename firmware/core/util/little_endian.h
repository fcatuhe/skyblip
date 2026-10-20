#ifndef SKYBLIP_CORE_UTIL_LITTLE_ENDIAN_H
#define SKYBLIP_CORE_UTIL_LITTLE_ENDIAN_H

#include <cstdint>

namespace skyblip {

inline void put_uint16(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
}

inline void put_uint24(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
    out[2] = static_cast<uint8_t>(value >> 16);
}

inline void put_uint32(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
    out[2] = static_cast<uint8_t>(value >> 16);
    out[3] = static_cast<uint8_t>(value >> 24);
}

inline void put_int16(uint8_t* out, int16_t value) {
    put_uint16(out, static_cast<uint16_t>(value));
}
inline void put_int32(uint8_t* out, int32_t value) {
    put_uint32(out, static_cast<uint32_t>(value));
}

inline uint16_t get_uint16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8));
}

inline uint32_t get_uint24(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16);
}

inline uint32_t get_uint32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

inline int16_t get_int16(const uint8_t* bytes) { return static_cast<int16_t>(get_uint16(bytes)); }
inline int32_t get_int32(const uint8_t* bytes) { return static_cast<int32_t>(get_uint32(bytes)); }

}

#endif
