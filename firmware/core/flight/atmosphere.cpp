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

constexpr int32_t kAltCm[] = {
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

constexpr int kN = static_cast<int>(sizeof(kAltCm) / sizeof(kAltCm[0]));
constexpr uint32_t kTableHighestPa = kTableLowestPa + static_cast<uint32_t>(kN - 1) * kStepPa;

constexpr int64_t kIsaLapseDeciCelsiusPerKm = 65;
constexpr int64_t kMmPerKm = 1000000;

constexpr uint32_t kMilli = 1000;
constexpr uint32_t kTableLowestMpa = kTableLowestPa * kMilli;
constexpr uint32_t kTableHighestMpa = kTableHighestPa * kMilli;
constexpr uint32_t kStepMpa = kStepPa * kMilli;

constexpr int32_t alt_mm_at(int i) { return kAltCm[i] * 10; }

}  // namespace

int32_t pressure_to_alt_mm(uint32_t pressure_mpa) {
    if (pressure_mpa <= kTableLowestMpa) return alt_mm_at(0);
    if (pressure_mpa >= kTableHighestMpa) return alt_mm_at(kN - 1);

    const uint32_t offset_mpa = pressure_mpa - kTableLowestMpa;
    const int i = static_cast<int>(offset_mpa / kStepMpa);
    const uint32_t frac = offset_mpa % kStepMpa;
    if (frac == 0) return alt_mm_at(i);

    // Table descends with pressure, so the step is negative. Interpolate on it.
    const int64_t span = alt_mm_at(i + 1) - alt_mm_at(i);
    return alt_mm_at(i) + static_cast<int32_t>(div_round<int64_t>(span * frac, kStepMpa));
}

int16_t isa_temperature_dc(int32_t alt_mm) {
    const int64_t drop =
        div_round<int64_t>(static_cast<int64_t>(alt_mm) * kIsaLapseDeciCelsiusPerKm, kMmPerKm);
    return static_cast<int16_t>(kIsaSeaLevelDeciCelsius - drop);
}

int32_t pressure_to_alt_cm(uint32_t pressure_pa) {
    if (pressure_pa >= kTableHighestPa) return kAltCm[kN - 1];
    return div_round(pressure_to_alt_mm(pressure_pa * kMilli), 10);
}

uint32_t alt_mm_to_pressure_mpa(int32_t alt_mm) {
    if (alt_mm >= alt_mm_at(0)) return kTableLowestMpa;
    if (alt_mm <= alt_mm_at(kN - 1)) return kTableHighestMpa;

    uint32_t lo = kTableLowestMpa, hi = kTableHighestMpa;
    while (hi - lo > 1) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (pressure_to_alt_mm(mid) > alt_mm)
            lo = mid;  // still too high up: raise the pressure
        else
            hi = mid;
    }
    return hi;
}

uint32_t alt_cm_to_pressure_pa(int32_t alt_cm) {
    return (alt_mm_to_pressure_mpa(alt_cm * 10) + kMilli / 2) / kMilli;
}

bool climb_mm_s_from_alt(int32_t alt_mm_now, int32_t alt_mm_then, uint32_t dt_ms,
                         int32_t& out_mm_s) {
    if (dt_ms < kClimbWindowFloorMs || dt_ms > kClimbWindowCeilingMs) return false;

    const int64_t d_mm = static_cast<int64_t>(alt_mm_now) - alt_mm_then;
    out_mm_s = static_cast<int32_t>(div_round<int64_t>(d_mm * 1000, static_cast<int64_t>(dt_ms)));
    return true;
}

int16_t climb_e8_from_mm_s(int32_t mm_s) { return to_climb_e8(MillimetresPerSec(mm_s)).v; }

}  // namespace skyblip::flight
