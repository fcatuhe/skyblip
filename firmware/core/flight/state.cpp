#include "core/flight/state.h"

#include "protocols/common/geometry.h"

namespace skyblip::flight {

namespace {

int32_t derated(int32_t evidence, uint16_t hdop_e2) {
    if (hdop_e2 <= kDopUnityE2) return evidence;
    return wire::divide_rounded(evidence * kDopUnityE2, static_cast<int32_t>(hdop_e2));
}

enum class G13Category : uint8_t {
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

}  // namespace

bool flight_evidence(const FlightSample& sample) {
    return derated(sample.speed_mm_s, sample.hdop_e2) >= kFlightSpeedMmS;
}

bool ground_evidence(const FlightSample& sample) { return sample.speed_mm_s < kGroundSpeedMmS; }

bool taxi_evidence(const FlightSample& sample) { return sample.speed_mm_s < kLandingSpeedMmS; }

bool FlightMonitor::jerky(int32_t previous_mm_s, int32_t now_mm_s) {
    return now_mm_s > previous_mm_s * kJerkSpeedRatio || previous_mm_s > now_mm_s * kJerkSpeedRatio;
}

void FlightMonitor::update_rolling(int32_t speed_mm_s) {
    if (speed_mm_s >= kRollingSpeedMmS)
        rolling_ = true;
    else if (speed_mm_s < kGroundSpeedMmS)
        rolling_ = false;
}

void FlightMonitor::update_taxi(const FlightSample& sample, uint32_t now_ms) {
    if (!sample.fix_valid || !taxi_evidence(sample)) {
        taxiing_ = false;
        return;
    }
    if (!taxiing_) {
        taxiing_ = true;
        taxiing_since_ms_ = now_ms;
    }
}

bool FlightMonitor::taxi_sustained(uint32_t now_ms) const {
    return taxiing_ && now_ms - taxiing_since_ms_ >= kLandingHoldMs;
}

FlightState FlightMonitor::update(const FlightSample& sample, uint32_t now_ms) {
    update_taxi(sample, now_ms);
    if (!sample.fix_valid) {
        previous_speed_valid_ = false;
        return FlightState::Unknown;
    }
    update_rolling(sample.speed_mm_s);

    const int32_t previous_speed_mm_s = last_speed_mm_s_;
    const bool comparable = previous_speed_valid_;
    previous_speed_valid_ = true;
    last_speed_mm_s_ = sample.speed_mm_s;

    if (state_ == FlightState::Unknown) {
        state_ = flight_evidence(sample) ? FlightState::Airborne : FlightState::Ground;
        return state_;
    }

    if (state_ == FlightState::Airborne) {
        if (ground_evidence(sample) || taxi_sustained(now_ms)) state_ = FlightState::Ground;
        return state_;
    }

    if (flight_evidence(sample) && !(comparable && jerky(previous_speed_mm_s, sample.speed_mm_s)))
        state_ = FlightState::Airborne;
    return state_;
}

bool flies_at_no_ground_speed(uint8_t aircraft_category) {
    switch (static_cast<G13Category>(aircraft_category)) {
        case G13Category::LightRotorcraft:
        case G13Category::LighterThanAir:
        case G13Category::Paraglider:
        case G13Category::Parachutist:
        case G13Category::Evtol:
        case G13Category::Gyrocopter:
        case G13Category::UasOpen:
        case G13Category::UasSpecific:
        case G13Category::UasCertified:
        case G13Category::HeavyRotorcraft:
        case G13Category::HangGlider:
        case G13Category::Paramotor: return true;
    }
    return false;
}

uint8_t announced_state(uint8_t code, uint8_t aircraft_category) {
    if (state_from(code) == FlightState::Ground && flies_at_no_ground_speed(aircraft_category))
        return static_cast<uint8_t>(FlightState::Unknown);
    return code;
}

}  // namespace skyblip::flight
