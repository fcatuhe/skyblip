#include "core/util/intmath.h"

namespace skyblip {

int16_t arctangent(int32_t y, int32_t x) {
    if (x == 0 && y == 0) return 0;
    int32_t absolute_x = x < 0 ? -x : x;
    int32_t absolute_y = y < 0 ? -y : y;
    int32_t angle;
    if (absolute_x >= absolute_y) {
        int64_t ratio = (static_cast<int64_t>(absolute_y) << 14) / (absolute_x ? absolute_x : 1);
        angle = static_cast<int32_t>((ratio * 0x2000) >> 14);
        angle += static_cast<int32_t>(((ratio * (0x4000 - ratio)) >> 14) * 0x0AAA >> 14);
    } else {
        int64_t ratio = (static_cast<int64_t>(absolute_x) << 14) / (absolute_y ? absolute_y : 1);
        angle = static_cast<int32_t>((ratio * 0x2000) >> 14);
        angle += static_cast<int32_t>(((ratio * (0x4000 - ratio)) >> 14) * 0x0AAA >> 14);
        angle = 0x4000 - angle;
    }
    if (x < 0) angle = 0x8000 - angle;
    if (y < 0) angle = -angle;
    return static_cast<int16_t>(angle & 0xFFFF);
}

}
