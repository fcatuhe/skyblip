#ifndef SKYBLIP_CORE_TRAFFIC_LEASE_H
#define SKYBLIP_CORE_TRAFFIC_LEASE_H

#include <cstdint>

#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/traffic/alarm.h"
#include "core/units/units.h"

namespace skyblip::traffic {

constexpr uint32_t kTargetForgetReports = 6;
// INFO: fc 12nov26 our second lags our clock by one, a paired Bravo's runs one ahead: no further
constexpr int32_t kReceptionAheadCeilingS = 2;
constexpr uint32_t kAirborneTargetForgetS = kTargetForgetReports * adsl::kAirborneReportPeriodS;
constexpr uint32_t kGroundTargetForgetS = kTargetForgetReports * adsl::kGroundReportPeriodS;

inline uint32_t age_s(uint32_t heard_s, uint32_t now_s) {
    const int32_t age = static_cast<int32_t>(now_s - heard_s);
    if (age >= 0) return static_cast<uint32_t>(age);
    return age >= -kReceptionAheadCeilingS ? 0u : UINT32_MAX;
}

inline uint32_t report_period_s(const model::AircraftObservation& observation) {
    return adsl::report_period_s(flight::state_from(observation.flight_state));
}

inline uint32_t forget_s(const model::AircraftObservation& observation) {
    return kTargetForgetReports * report_period_s(observation);
}

inline uint32_t forget_ms(const model::AircraftObservation& observation) {
    return forget_s(observation) * kMillisecondsPerSecond;
}

inline uint32_t direct_preferred_max_age_s(const model::AircraftObservation& observation) {
    const uint32_t alert_s = kAlertMaxAgeMs / kMillisecondsPerSecond;
    const uint32_t period_s = report_period_s(observation);
    return period_s > alert_s ? period_s : alert_s;
}

}  // namespace skyblip::traffic

#endif
