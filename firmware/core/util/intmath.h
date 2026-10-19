// core/util/intmath.h: fixed-point integer trig & distance. Fixed-point keeps
#ifndef SKYBLIP_CORE_UTIL_INTMATH_H
#define SKYBLIP_CORE_UTIL_INTMATH_H

#include <cstdint>

namespace skyblip {

// INFO: fc 19sep26 integer division truncates toward zero, which reads a measurement a unit low
template <class T>
constexpr T div_round(T number, T divisor) {
    return number >= 0 ? (number + divisor / 2) / divisor : -((-number + divisor / 2) / divisor);
}

constexpr int64_t kE7PerTurn = 3600000000LL;

constexpr int32_t wrapped_longitude_1e7(int64_t longitude_1e7) {
    const int64_t half_turn = kE7PerTurn / 2;
    return static_cast<int32_t>(
        ((longitude_1e7 + half_turn) % kE7PerTurn + kE7PerTurn) % kE7PerTurn - half_turn);
}

constexpr int32_t kTrigOne = 16384;

int16_t sine(int16_t angle);
inline int16_t cosine(int16_t angle) { return sine(static_cast<int16_t>(angle + 0x4000)); }

int16_t arctangent(int32_t y, int32_t x);

template <class T>
T square_root(T square) {
    T root = 0;
    T mask = static_cast<T>(1) << (sizeof(T) * 8 - 2);
    while (mask > square) mask >>= 2;
    while (mask) {
        if (square >= (root + mask)) {
            square -= root + mask;
            root += mask << 1;
        }
        root >>= 1;
        mask >>= 2;
    }
    if (square > root) root++;
    return root;
}

inline uint32_t hypotenuse(int32_t dx, int32_t dy) {
    return static_cast<uint32_t>(square_root<uint64_t>(
        static_cast<uint64_t>(static_cast<int64_t>(dx) * dx + static_cast<int64_t>(dy) * dy)));
}

}

#endif
