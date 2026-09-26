// core/util/intmath.h: fixed-point integer trig & distance. Fixed-point keeps
#ifndef SKYBLIP_CORE_UTIL_INTMATH_H
#define SKYBLIP_CORE_UTIL_INTMATH_H

#include <cstdint>

namespace skyblip {

// INFO: fc 19sep26 integer division truncates toward zero, which reads a measurement a unit low
template <class T>
constexpr T div_round(T num, T den) {
    return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
}

constexpr int64_t kE7PerTurn = 3600000000LL;

constexpr int32_t wrapped_lon_1e7(int64_t lon_1e7) {
    const int64_t half_turn = kE7PerTurn / 2;
    return static_cast<int32_t>(((lon_1e7 + half_turn) % kE7PerTurn + kE7PerTurn) % kE7PerTurn -
                                half_turn);
}

int16_t isin(int16_t angle);
inline int16_t icos(int16_t angle) { return isin(static_cast<int16_t>(angle + 0x4000)); }

int16_t iatan2(int32_t y, int32_t x);

template <class T>
T isqrt(T inp) {
    T out = 0;
    T mask = static_cast<T>(1) << (sizeof(T) * 8 - 2);
    while (mask > inp) mask >>= 2;
    while (mask) {
        if (inp >= (out + mask)) {
            inp -= out + mask;
            out += mask << 1;
        }
        out >>= 1;
        mask >>= 2;
    }
    if (inp > out) out++;
    return out;
}

inline uint32_t idistance(int32_t dx, int32_t dy) {
    uint64_t d = isqrt<uint64_t>(
        static_cast<uint64_t>(static_cast<int64_t>(dx) * dx + static_cast<int64_t>(dy) * dy));
    return static_cast<uint32_t>(d);
}

int32_t ifast_distance(int32_t dx, int32_t dy);

}

#endif
