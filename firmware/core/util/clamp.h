#ifndef SKYBLIP_CORE_UTIL_CLAMP_H
#define SKYBLIP_CORE_UTIL_CLAMP_H

#include <algorithm>
#include <cstdint>
#include <limits>

namespace skyblip {

template <class Narrow, class Wide>
constexpr Narrow clamped(Wide value) {
    return static_cast<Narrow>(std::clamp<Wide>(value, std::numeric_limits<Narrow>::min(),
                                                std::numeric_limits<Narrow>::max()));
}

constexpr int8_t clamp_int8(int32_t value) { return clamped<int8_t>(value); }
constexpr int16_t clamp_int16(int32_t value) { return clamped<int16_t>(value); }
constexpr uint16_t clamp_uint16(uint32_t value) { return clamped<uint16_t>(value); }
constexpr int32_t clamp_int32(int64_t value) { return clamped<int32_t>(value); }

}

#endif
