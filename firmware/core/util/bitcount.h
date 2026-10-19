// core/util/bitcount.h: popcount over a byte, a word or a buffer, for counting
// the bits a syndrome disagrees on. Compiler builtins, so a target with a
// popcount instruction uses it.
#ifndef SKYBLIP_CORE_UTIL_BITCOUNT_H
#define SKYBLIP_CORE_UTIL_BITCOUNT_H

#include <cstddef>
#include <cstdint>

namespace skyblip {

inline uint8_t count_ones(uint8_t value) { return static_cast<uint8_t>(__builtin_popcount(value)); }
inline uint8_t count_ones(uint16_t value) {
    return static_cast<uint8_t>(__builtin_popcount(value));
}
inline uint8_t count_ones(uint32_t value) {
    return static_cast<uint8_t>(__builtin_popcountl(value));
}
inline uint8_t count_ones(uint64_t value) {
    return static_cast<uint8_t>(__builtin_popcountll(value));
}

inline uint32_t count_ones(const uint8_t* data, size_t bytes) {
    uint32_t sum = 0;
    for (size_t i = 0; i < bytes; i++) sum += count_ones(data[i]);
    return sum;
}

}

#endif
