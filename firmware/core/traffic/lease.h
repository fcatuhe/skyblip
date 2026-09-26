#ifndef SKYBLIP_CORE_TRAFFIC_LEASE_H
#define SKYBLIP_CORE_TRAFFIC_LEASE_H

#include <cstdint>

#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/traffic/alarm.h"
#include "core/units/units.h"

namespace skyblip::traffic {

constexpr uint32_t kTargetForgetReports = 6;
constexpr uint32_t kAirborneTargetForgetS = kTargetForgetReports * flight::kAirborneReportPeriodS;
constexpr uint32_t kGroundTargetForgetS = kTargetForgetReports * flight::kGroundReportPeriodS;

inline uint32_t report_period_s(const model::AircraftObs& obs) {
    return flight::report_period_s(flight::state_from(obs.flight_state));
}

inline uint32_t forget_s(const model::AircraftObs& obs) {
    return kTargetForgetReports * report_period_s(obs);
}

inline uint32_t forget_ms(const model::AircraftObs& obs) {
    return forget_s(obs) * kMillisecondsPerSecond;
}

inline uint32_t direct_preferred_max_age_s(const model::AircraftObs& obs) {
    const uint32_t alert_s = kAlertMaxAgeMs / kMillisecondsPerSecond;
    const uint32_t period_s = report_period_s(obs);
    return period_s > alert_s ? period_s : alert_s;
}

}  // namespace skyblip::traffic

#endif
