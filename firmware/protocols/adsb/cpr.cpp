#include "protocols/adsb/cpr.h"

#include "protocols/common/geometry.h"

namespace skyblip::adsb {

using wire::kE7PerTurn;
using wire::wrapped_longitude_1e7;

namespace {

constexpr int64_t kCprSteps = int64_t{1} << 17;
constexpr int64_t kHalfCprStep = kCprSteps / 2;
constexpr int64_t kLatitudeZones = 60;
constexpr int64_t kQuarterTurn1e7 = kE7PerTurn / 4;
constexpr int64_t kSouthernTurn1e7 = kE7PerTurn * 3 / 4;
constexpr int kMostLongitudeZones = 59;

// INFO: fc 10nov26 the transition latitudes of DO-260B draft A.1.7.2 Note 5, for NL 59 down to 2
constexpr int32_t kTransitionLatitudes1e7[] = {
    104704713, 148281744, 181862636, 210293949, 235450449, 258292471, 279389871, 299113569,
    317720971, 335399344, 352289960, 368502511, 384124189, 399225668, 413865183, 428091401,
    441945495, 455462672, 468673325, 481603913, 494277644, 506715017, 518934247, 530951615,
    542781747, 554437844, 565931876, 577274735, 588476378, 599545928, 610491777, 621321666,
    632042748, 642661652, 653184531, 663617101, 673964677, 684232202, 694424263, 704545107,
    714598647, 724588454, 734517744, 744389342, 754205626, 763968439, 773678946, 783337408,
    792942823, 802492321, 811980135, 821395698, 830719944, 839917356, 848916619, 857554162,
    865353700, 870000000,
};

int64_t floor_divided(int64_t number, int64_t divisor) {
    const int64_t quotient = number / divisor;
    return (number % divisor != 0 && number < 0) ? quotient - 1 : quotient;
}

int64_t modulo(int64_t number, int64_t divisor) { return ((number % divisor) + divisor) % divisor; }

int64_t turn_of(CprFormat format) {
    return format == CprFormat::Surface ? kQuarterTurn1e7 : kE7PerTurn;
}

// INFO: fc 10nov26 half up, never away from zero, so a zone shifted by a turn rounds the same way
int64_t angle_1e7(int64_t turn_1e7, int64_t zone, uint32_t coordinate_cpr, int64_t zones) {
    return floor_divided(2 * turn_1e7 * (zone * kCprSteps + coordinate_cpr) + zones * kCprSteps,
                         2 * zones * kCprSteps);
}

int64_t zone_near(int64_t reference_1e7, int64_t zones, uint32_t coordinate_cpr, int64_t turn_1e7) {
    return floor_divided(
        reference_1e7 * zones * kCprSteps - coordinate_cpr * turn_1e7 + turn_1e7 * kHalfCprStep,
        turn_1e7 * kCprSteps);
}

int64_t nearest_of(int64_t first_1e7, int64_t step_1e7, int count, int64_t reference_1e7,
                   bool longitude) {
    int64_t nearest = first_1e7;
    int64_t nearest_gap = -1;
    for (int k = 0; k < count; k++) {
        const int64_t candidate = first_1e7 + k * step_1e7;
        int64_t gap = candidate - reference_1e7;
        if (longitude) gap = wrapped_longitude_1e7(gap);
        if (gap < 0) gap = -gap;
        if (nearest_gap < 0 || gap < nearest_gap) {
            nearest = candidate;
            nearest_gap = gap;
        }
    }
    return nearest;
}

int64_t global_latitude_1e7(CprFormat format, int64_t northern_1e7, int64_t reference_1e7) {
    if (format == CprFormat::Surface)
        return nearest_of(northern_1e7 - kQuarterTurn1e7, kQuarterTurn1e7, 2, reference_1e7, false);
    return northern_1e7 >= kSouthernTurn1e7 ? northern_1e7 - kE7PerTurn : northern_1e7;
}

bool on_the_globe(int64_t latitude_1e7) {
    return latitude_1e7 >= -kQuarterTurn1e7 && latitude_1e7 <= kQuarterTurn1e7;
}

}

int cpr_longitude_zones(int32_t latitude_1e7) {
    const int64_t absolute_1e7 = latitude_1e7 < 0 ? -int64_t{latitude_1e7} : latitude_1e7;
    int zones = kMostLongitudeZones;
    for (int32_t transition_1e7 : kTransitionLatitudes1e7) {
        if (absolute_1e7 <= transition_1e7) return zones;
        zones--;
    }
    return 1;
}

bool cpr_global(CprFormat format, const Cpr& newest, const Cpr& other, const CprPosition& near,
                CprPosition& out) {
    if (newest.odd == other.odd) return false;
    const Cpr& even = newest.odd ? other : newest;
    const Cpr& odd = newest.odd ? newest : other;
    const int64_t turn_1e7 = turn_of(format);

    const int64_t latitude_zone = floor_divided(
        (kLatitudeZones - 1) * even.latitude_cpr - kLatitudeZones * odd.latitude_cpr + kHalfCprStep,
        kCprSteps);
    const int64_t even_latitude_1e7 =
        global_latitude_1e7(format,
                            angle_1e7(turn_1e7, modulo(latitude_zone, kLatitudeZones),
                                      even.latitude_cpr, kLatitudeZones),
                            near.latitude_1e7);
    const int64_t odd_latitude_1e7 =
        global_latitude_1e7(format,
                            angle_1e7(turn_1e7, modulo(latitude_zone, kLatitudeZones - 1),
                                      odd.latitude_cpr, kLatitudeZones - 1),
                            near.latitude_1e7);
    if (!on_the_globe(even_latitude_1e7) || !on_the_globe(odd_latitude_1e7)) return false;

    const int zones = cpr_longitude_zones(static_cast<int32_t>(even_latitude_1e7));
    if (zones != cpr_longitude_zones(static_cast<int32_t>(odd_latitude_1e7))) return false;

    const int64_t parity = newest.odd ? 1 : 0;
    const int64_t longitude_zones = zones - parity > 1 ? zones - parity : 1;
    const int64_t longitude_zone =
        floor_divided(int64_t{even.longitude_cpr} * (zones - 1) -
                          int64_t{odd.longitude_cpr} * zones + kHalfCprStep,
                      kCprSteps);
    int64_t longitude_1e7 = angle_1e7(turn_1e7, modulo(longitude_zone, longitude_zones),
                                      newest.longitude_cpr, longitude_zones);
    if (format == CprFormat::Surface)
        longitude_1e7 = nearest_of(longitude_1e7, kQuarterTurn1e7, 4, near.longitude_1e7, true);

    out.latitude_1e7 = static_cast<int32_t>(newest.odd ? odd_latitude_1e7 : even_latitude_1e7);
    out.longitude_1e7 = wrapped_longitude_1e7(longitude_1e7);
    return true;
}

bool cpr_local(CprFormat format, const Cpr& cpr, const CprPosition& reference, CprPosition& out) {
    const int64_t turn_1e7 = turn_of(format);
    const int64_t parity = cpr.odd ? 1 : 0;
    const int64_t latitude_zones = kLatitudeZones - parity;
    const int64_t latitude_1e7 = angle_1e7(
        turn_1e7, zone_near(reference.latitude_1e7, latitude_zones, cpr.latitude_cpr, turn_1e7),
        cpr.latitude_cpr, latitude_zones);
    if (!on_the_globe(latitude_1e7)) return false;

    const int zones = cpr_longitude_zones(static_cast<int32_t>(latitude_1e7));
    const int64_t longitude_zones = zones - parity > 1 ? zones - parity : 1;
    const int64_t longitude_1e7 = angle_1e7(
        turn_1e7, zone_near(reference.longitude_1e7, longitude_zones, cpr.longitude_cpr, turn_1e7),
        cpr.longitude_cpr, longitude_zones);

    out.latitude_1e7 = static_cast<int32_t>(latitude_1e7);
    out.longitude_1e7 = wrapped_longitude_1e7(longitude_1e7);
    return true;
}

}
