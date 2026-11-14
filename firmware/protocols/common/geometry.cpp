#include "protocols/common/geometry.h"

namespace skyblip::wire {

namespace {
constexpr int64_t kMicrometresPerMetre = 1000000;

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

NorthEast north_east_m(int32_t from_latitude_1e7, int32_t from_longitude_1e7,
                       int32_t to_latitude_1e7, int32_t to_longitude_1e7) {
    const int64_t latitude_delta_1e7 = static_cast<int64_t>(to_latitude_1e7) - from_latitude_1e7;
    const int64_t longitude_delta_1e7 =
        wrapped_longitude_1e7(static_cast<int64_t>(to_longitude_1e7) - from_longitude_1e7);
    NorthEast out;
    out.north_m = static_cast<int32_t>(
        divide_rounded<int64_t>(latitude_delta_1e7 * kMicrometresPerE7, kMicrometresPerMetre));
    const int64_t cosine_of_latitude = cosine(latitude_angle16(from_latitude_1e7));
    const int64_t east_micrometres = divide_rounded<int64_t>(
        longitude_delta_1e7 * kMicrometresPerE7 * cosine_of_latitude, kTrigOne);
    out.east_m =
        static_cast<int32_t>(divide_rounded<int64_t>(east_micrometres, kMicrometresPerMetre));
    return out;
}

}
