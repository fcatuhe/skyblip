#include "core/flight/atmosphere.h"

#include <algorithm>

#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::flight {

namespace {

// ISA troposphere barometric formula, tabulated:
//   h = 44330.77 * (1 - (p/101325)^0.190263)   [ICAO standard atmosphere]
// 500 Pa steps, altitude in centimetres. Worst linear-interpolation error
// between entries is 24 cm, which is under the noise of any real sensor.
constexpr uint32_t kTableLowestPa = 26000;
constexpr uint32_t kStepPa = 500;

constexpr int32_t kAltitudeCm[] = {
    1010851, 998426, 986189, 974134, 962256, 950548, 939005, 927622, 916395, 905318, 894387, 883597,
    872946,  862429, 852041, 841781, 831644, 821626, 811726, 801939, 792264, 782696, 773235, 763876,
    754617,  745457, 736393, 727422, 718543, 709753, 701051, 692434, 683900, 675449, 667078, 658785,
    650569,  642428, 634361, 626367, 618443, 610588, 602802, 595082, 587428, 579838, 572312, 564847,
    557443,  550099, 542813, 535585, 528414, 521298, 514237, 507230, 500275, 493373, 486521, 479720,
    472968,  466265, 459610, 453002, 446440, 439924, 433452, 427025, 420642, 414302, 408003, 401747,
    395531,  389356, 383221, 377126, 371068, 365050, 359068, 353124, 347217, 341345, 335510, 329709,
    323943,  318212, 312514, 306849, 301218, 295619, 290052, 284516, 279012, 273539, 268096, 262683,
    257300,  251947, 246622, 241326, 236059, 230819, 225607, 220423, 215265, 210134, 205030, 199951,
    194899,  189872, 184870, 179893, 174940, 170013, 165109, 160229, 155373, 150540, 145730, 140943,
    136179,  131437, 126717, 122019, 117342, 112688, 108054, 103442, 98850,  94279,  89728,  85198,
    80687,   76197,  71726,  67274,  62842,  58428,  54034,  49658,  45301,  40962,  36641,  32338,
    28053,   23786,  19536,  15304,  11088,  6890,   2709,   -1456,  -5604,  -9735,  -13851, -17950,
    -22033,  -26100, -30152, -34188, -38208, -42214, -46204, -50178, -54138, -58084, -62014, -65930,
    -69831,
};

constexpr int kTableEntries = static_cast<int>(sizeof(kAltitudeCm) / sizeof(kAltitudeCm[0]));
constexpr uint32_t kTableHighestPa =
    kTableLowestPa + static_cast<uint32_t>(kTableEntries - 1) * kStepPa;

constexpr int64_t kIsaLapseDeciCelsiusPerKilometre = 65;
constexpr int64_t kMillimetresPerKilometre = 1000000;

constexpr uint32_t kMillipascalsPerPascal = 1000;
constexpr uint32_t kTableLowestMpa = kTableLowestPa * kMillipascalsPerPascal;
constexpr uint32_t kTableHighestMpa = kTableHighestPa * kMillipascalsPerPascal;
constexpr uint32_t kStepMpa = kStepPa * kMillipascalsPerPascal;

constexpr int32_t altitude_mm_at(int i) { return kAltitudeCm[i] * 10; }

}  // namespace

int32_t pressure_to_altitude_mm(uint32_t pressure_mpa) {
    if (pressure_mpa <= kTableLowestMpa) return altitude_mm_at(0);
    if (pressure_mpa >= kTableHighestMpa) return altitude_mm_at(kTableEntries - 1);

    const uint32_t offset_mpa = pressure_mpa - kTableLowestMpa;
    const int i = static_cast<int>(offset_mpa / kStepMpa);
    const uint32_t fraction = offset_mpa % kStepMpa;
    if (fraction == 0) return altitude_mm_at(i);

    // Table descends with pressure, so the step is negative. Interpolate on it.
    const int64_t span = altitude_mm_at(i + 1) - altitude_mm_at(i);
    return altitude_mm_at(i) + static_cast<int32_t>(div_round<int64_t>(span * fraction, kStepMpa));
}

int16_t isa_temperature_dc(int32_t altitude_mm) {
    const int64_t drop =
        div_round<int64_t>(static_cast<int64_t>(altitude_mm) * kIsaLapseDeciCelsiusPerKilometre,
                           kMillimetresPerKilometre);
    return static_cast<int16_t>(kIsaSeaLevelDeciCelsius - drop);
}

int32_t pressure_to_altitude_cm(uint32_t pressure_pa) {
    if (pressure_pa >= kTableHighestPa) return kAltitudeCm[kTableEntries - 1];
    return div_round(pressure_to_altitude_mm(pressure_pa * kMillipascalsPerPascal), 10);
}

uint32_t altitude_mm_to_pressure_mpa(int32_t altitude_mm) {
    if (altitude_mm >= altitude_mm_at(0)) return kTableLowestMpa;
    if (altitude_mm <= altitude_mm_at(kTableEntries - 1)) return kTableHighestMpa;

    uint32_t low = kTableLowestMpa, high = kTableHighestMpa;
    while (high - low > 1) {
        const uint32_t middle = low + (high - low) / 2;
        if (pressure_to_altitude_mm(middle) > altitude_mm)
            low = middle;  // still too high up: raise the pressure
        else
            high = middle;
    }
    return high;
}

uint32_t altitude_cm_to_pressure_pa(int32_t altitude_cm) {
    return (altitude_mm_to_pressure_mpa(altitude_cm * 10) + kMillipascalsPerPascal / 2) /
           kMillipascalsPerPascal;
}

bool climb_mm_s_from_altitude(int32_t altitude_mm_now, int32_t altitude_mm_then,
                              uint32_t elapsed_ms, int32_t& out_mm_s) {
    if (elapsed_ms < kClimbWindowFloorMs || elapsed_ms > kClimbWindowCeilingMs) return false;

    const int64_t climbed_mm = static_cast<int64_t>(altitude_mm_now) - altitude_mm_then;
    out_mm_s = static_cast<int32_t>(
        div_round<int64_t>(climbed_mm * 1000, static_cast<int64_t>(elapsed_ms)));
    return true;
}

int16_t climb_e8_from_mm_s(int32_t climb_mm_s) {
    return to_climb_e8(MillimetresPerSecond(climb_mm_s)).value;
}

}  // namespace skyblip::flight
