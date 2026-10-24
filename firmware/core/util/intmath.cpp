#include "core/util/intmath.h"

namespace skyblip {

namespace {
const int16_t kSineQuarter[65] = {
    0,     402,   804,   1205,  1606,  2006,  2404,  2801,  3196,  3590,  3981,  4370,  4756,
    5139,  5520,  5897,  6270,  6639,  7005,  7366,  7723,  8076,  8423,  8765,  9102,  9434,
    9760,  10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406, 12665, 12916, 13160,
    13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557,
    15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384};
}

int16_t sine(int16_t angle) {
    uint16_t turn = static_cast<uint16_t>(angle);
    uint16_t quadrant = (turn >> 14) & 3;
    uint16_t index = (turn >> 8) & 0x3F;
    uint16_t fraction = turn & 0xFF;
    int32_t low, high;
    switch (quadrant) {
        case 0:
            low = kSineQuarter[index];
            high = kSineQuarter[index + 1];
            break;
        case 1:
            low = kSineQuarter[64 - index];
            high = kSineQuarter[63 - index];
            break;
        case 2:
            low = -kSineQuarter[index];
            high = -kSineQuarter[index + 1];
            break;
        default:
            low = -kSineQuarter[64 - index];
            high = -kSineQuarter[63 - index];
            break;
    }
    return static_cast<int16_t>(low + ((high - low) * static_cast<int32_t>(fraction)) / 256);
}

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
