// core/util/variable_range.h: ADS-L "variable-range" field codecs: unsigned and signed range
#ifndef SKYBLIP_CORE_UTIL_VARIABLE_RANGE_H
#define SKYBLIP_CORE_UTIL_VARIABLE_RANGE_H

#include <cstdint>

namespace skyblip {

template <class T, int Bits>
T decode_unsigned_variable_range(T value) {
    const T threshold = static_cast<T>(1) << Bits;
    uint8_t range = value >> Bits;
    value &= threshold - 1;
    if (range == 0) return value;
    if (range == 1) return threshold + 1 + (value << 1);
    if (range == 2) return 3 * threshold + 2 + (value << 2);
    return 7 * threshold + 4 + (value << 3);
}

template <class T, int Bits>
T encode_unsigned_variable_range(T value) {
    const T threshold = static_cast<T>(1) << Bits;
    if (value < threshold) return value;
    if (value < 3 * threshold) return threshold | ((value - threshold) >> 1);
    if (value < 7 * threshold) return 2 * threshold | ((value - 3 * threshold) >> 2);
    if (value < 15 * threshold) return 3 * threshold | ((value - 7 * threshold) >> 3);
    return 4 * threshold - 1;
}

template <class T, int Bits>
T encode_signed_variable_range(T value) {
    const T sign_mask = static_cast<T>(1) << (Bits + 2);
    T sign = 0;
    if (value < 0) {
        value = -value;
        sign = sign_mask;
    }
    value = encode_unsigned_variable_range<T, Bits>(value);
    return value | sign;
}

template <class T, int Bits>
T decode_signed_variable_range(T value) {
    const T sign_mask = static_cast<T>(1) << (Bits + 2);
    T sign = value & sign_mask;
    value = decode_unsigned_variable_range<T, Bits>(value & (sign_mask - 1));
    return sign ? -value : value;
}

}

#endif
