#ifndef SKYBLIP_CORE_DIAG_PROFILE_H
#define SKYBLIP_CORE_DIAG_PROFILE_H

#include <cstdint>

#include "core/diag/payload.h"
#include "core/diag/record.h"

namespace skyblip::diag {

enum class Profile : uint8_t { Full = 0, PowerRun = 1 };

// INFO: fc 21sep26 45,220 slots at two records a pass is 188 h, against a 50 h run to cutoff
constexpr uint32_t kPowerRunRecordPeriodMs = 30000;
static_assert(kPowerRunRecordPeriodMs <= kDutyMaxPeriodMs,
              "two Duty records this far apart cannot be subtracted");

constexpr uint32_t kPowerRunRecordsPerPass = 2;

constexpr bool lists(Profile profile, Type type) {
    if (profile == Profile::Full) return type != Type::None;
    switch (type) {
        case Type::Boot:
        case Type::Config:
        case Type::Power:
        case Type::Duty:
        case Type::Gap:
        case Type::End: return true;
        case Type::None:
        case Type::Gnss:
        case Type::Pps:
        case Type::Burst:
        case Type::Dwell:
        case Type::Flight:
        case Type::Baro:
        case Type::Motion:
        case Type::Contact:
        case Type::Link:
        case Type::Traffic:
        case Type::Write:
        case Type::Screen: return false;
    }
    return false;
}

constexpr bool recurs(Type type) {
    return type != Type::None && type != Type::Boot && type != Type::Config && type != Type::Gap &&
           type != Type::End;
}

}  // namespace skyblip::diag

#endif
