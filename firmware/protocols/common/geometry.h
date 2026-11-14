#ifndef SKYBLIP_PROTOCOLS_COMMON_GEOMETRY_H
#define SKYBLIP_PROTOCOLS_COMMON_GEOMETRY_H

#include <cstdint>
#include <type_traits>

namespace skyblip::wire {

// INFO: fc 19sep26 integer division truncates toward zero, which reads a measurement a unit low
template <class T>
constexpr T divide_rounded(T number, T divisor) {
    return number >= 0 ? (number + divisor / 2) / divisor : -((-number + divisor / 2) / divisor);
}

constexpr int64_t kE7PerTurn = 3600000000LL;
// INFO: fc 10nov26 the tree's one size of the earth: 1e-7 degree of latitude is 11132 micrometres
constexpr int64_t kMicrometresPerE7 = 11132;

constexpr int32_t wrapped_longitude_1e7(int64_t longitude_1e7) {
    const int64_t half_turn = kE7PerTurn / 2;
    return static_cast<int32_t>(
        ((longitude_1e7 + half_turn) % kE7PerTurn + kE7PerTurn) % kE7PerTurn - half_turn);
}

constexpr int32_t kTrigOne = 16384;
// INFO: fc 19sep26 the angle the sine table is indexed by, 65536 to the turn
constexpr int32_t kAngle16PerTurn = 65536;

int16_t sine(int16_t angle);
inline int16_t cosine(int16_t angle) { return sine(static_cast<int16_t>(angle + 0x4000)); }

inline int16_t latitude_angle16(int32_t latitude_1e7) {
    return static_cast<int16_t>((static_cast<int64_t>(latitude_1e7) * kAngle16PerTurn) /
                                kE7PerTurn);
}

// INFO: fc 09nov26 rounds to the nearest root, and a negative square, which has none, gives 0
template <class T>
T square_root(T square) {
    if (square <= 0) return 0;
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned remainder = static_cast<Unsigned>(square);
    Unsigned root = 0;
    Unsigned place = Unsigned{1} << (sizeof(T) * 8 - 2);
    while (place > remainder) place >>= 2;
    for (; place != 0; place >>= 2) {
        if (remainder >= root + place) {
            remainder -= root + place;
            root = (root >> 1) + place;
        } else {
            root >>= 1;
        }
    }
    if (remainder > root) root++;
    return static_cast<T>(root);
}

inline uint32_t hypotenuse(int32_t delta_x, int32_t delta_y) {
    return static_cast<uint32_t>(square_root<uint64_t>(static_cast<uint64_t>(
        static_cast<int64_t>(delta_x) * delta_x + static_cast<int64_t>(delta_y) * delta_y)));
}

struct NorthEast {
    int32_t north_m{0};
    int32_t east_m{0};
};

NorthEast north_east_m(int32_t from_latitude_1e7, int32_t from_longitude_1e7,
                       int32_t to_latitude_1e7, int32_t to_longitude_1e7);

}

#endif
