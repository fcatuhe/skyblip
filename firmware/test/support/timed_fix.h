// Harness, not a test: the UTC every rig dates its fixes from, and a fix as a receiver reports one.
#ifndef SKYBLIP_TEST_SUPPORT_TIMED_FIX_H
#define SKYBLIP_TEST_SUPPORT_TIMED_FIX_H

#include <cstdint>

#include "core/gnss/nmea.h"

namespace skyblip {

// 2026-08-02T00:00:00Z. A fix with no UTC cannot name a session, so every fix a rig makes has one.
inline constexpr uint32_t kBaseUtcS = 1785628800;
inline constexpr int32_t kFixLatitude1e7 = 485000000;
inline constexpr int32_t kFixLongitude1e7 = 85000000;

struct FixAt {
    uint32_t utc_offset_s;
    uint32_t sentences;
    int32_t latitude_1e7 = kFixLatitude1e7;
    int32_t altitude_msl_mm = 0;
    int32_t speed_mm_s = 0;
    int32_t track_cdeg = 0;
};

// Timed, solved in 3D on ten satellites, and referenced to both datums: core/gnss decides the rest.
inline gnss::GnssSolution timed_fix(const FixAt& at) {
    gnss::GnssSolution solution{};
    solution.fix_valid = true;
    solution.utc_valid = true;
    solution.utc_s = kBaseUtcS + at.utc_offset_s;
    solution.latitude_1e7 = at.latitude_1e7;
    solution.longitude_1e7 = kFixLongitude1e7;
    solution.altitude_msl_mm = at.altitude_msl_mm;
    solution.altitude_hae_mm = at.altitude_msl_mm + gnss::kDefaultGeoidSeparationMm;
    solution.geoid_measured = true;
    solution.speed_mm_s = at.speed_mm_s;
    solution.track_cdeg = at.track_cdeg;
    solution.satellites = 10;
    solution.hdop_e2 = 100;
    solution.vdop_e2 = 150;
    solution.sentences = at.sentences;
    return solution;
}

}  // namespace skyblip

#endif
