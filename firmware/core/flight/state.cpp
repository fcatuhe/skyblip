#include "core/flight/state.h"

#include "core/util/intmath.h"

namespace skyblip::flight {

namespace {

int32_t derated(int32_t evidence, uint16_t hdop_e2) {
    if (hdop_e2 <= kDopUnityE2) return evidence;
    return div_round(evidence * kDopUnityE2, static_cast<int32_t>(hdop_e2));
}

enum class AdslG13Category : uint8_t {
    LightRotorcraft = 3,
    LighterThanAir = 5,
    Paraglider = 7,
    Parachutist = 8,
    Evtol = 9,
    Gyrocopter = 10,
    UasOpen = 11,
    UasSpecific = 12,
    UasCertified = 13,
    HeavyRotorcraft = 15,
    HangGlider = 16,
    Paramotor = 17,
};

bool flies_at_no_ground_speed(uint8_t aircraft_cat) {
    switch (static_cast<AdslG13Category>(aircraft_cat)) {
        case AdslG13Category::LightRotorcraft:
        case AdslG13Category::LighterThanAir:
        case AdslG13Category::Paraglider:
        case AdslG13Category::Parachutist:
        case AdslG13Category::Evtol:
        case AdslG13Category::Gyrocopter:
        case AdslG13Category::UasOpen:
        case AdslG13Category::UasSpecific:
        case AdslG13Category::UasCertified:
        case AdslG13Category::HeavyRotorcraft:
        case AdslG13Category::HangGlider:
        case AdslG13Category::Paramotor: return true;
    }
    return false;
}

}  // namespace

bool flight_evidence(const FlightSample& sample) {
    return derated(sample.speed_mm_s, sample.hdop_e2) >= kFlightSpeedMmS;
}

bool ground_evidence(const FlightSample& sample) { return sample.speed_mm_s < kGroundSpeedMmS; }

bool taxi_evidence(const FlightSample& sample) { return sample.speed_mm_s < kLandingSpeedMmS; }

FlightState state_from(uint8_t adsl_code) {
    switch (static_cast<FlightState>(adsl_code)) {
        case FlightState::OnGround: return FlightState::OnGround;
        case FlightState::Airborne: return FlightState::Airborne;
        case FlightState::Unknown: break;
    }
    return FlightState::Unknown;
}

uint8_t announced_state(uint8_t adsl_code, uint8_t aircraft_cat) {
    if (state_from(adsl_code) == FlightState::OnGround && flies_at_no_ground_speed(aircraft_cat))
        return static_cast<uint8_t>(FlightState::Unknown);
    return adsl_code;
}

bool FlightMonitor::jerky(int32_t previous_mm_s, int32_t now_mm_s) {
    return now_mm_s > previous_mm_s * kJerkSpeedRatio || previous_mm_s > now_mm_s * kJerkSpeedRatio;
}

void FlightMonitor::update_rolling(int32_t speed_mm_s) {
    if (speed_mm_s >= kTaxiSpeedMmS)
        rolling_ = true;
    else if (speed_mm_s < kGroundSpeedMmS)
        rolling_ = false;
}

void FlightMonitor::update_slowdown(const FlightSample& sample, uint32_t now_ms) {
    if (!sample.fix_valid || !taxi_evidence(sample)) {
        slow_ = false;
        return;
    }
    if (!slow_) {
        slow_ = true;
        slow_since_ms_ = now_ms;
    }
}

bool FlightMonitor::taxi_sustained(uint32_t now_ms) const {
    return slow_ && now_ms - slow_since_ms_ >= kLandingHoldMs;
}

FlightState FlightMonitor::update(const FlightSample& sample, uint32_t now_ms) {
    update_slowdown(sample, now_ms);
    if (!sample.fix_valid) {
        armed_ = false;
        return FlightState::Unknown;
    }
    update_rolling(sample.speed_mm_s);

    const int32_t previous_speed_mm_s = last_speed_mm_s_;
    const bool comparable = armed_;
    armed_ = true;
    last_speed_mm_s_ = sample.speed_mm_s;

    if (state_ == FlightState::Unknown) {
        state_ = flight_evidence(sample) ? FlightState::Airborne : FlightState::OnGround;
        return state_;
    }

    if (state_ == FlightState::Airborne) {
        if (ground_evidence(sample) || taxi_sustained(now_ms)) state_ = FlightState::OnGround;
        return state_;
    }

    if (flight_evidence(sample) && !(comparable && jerky(previous_speed_mm_s, sample.speed_mm_s)))
        state_ = FlightState::Airborne;
    return state_;
}

}  // namespace skyblip::flight
