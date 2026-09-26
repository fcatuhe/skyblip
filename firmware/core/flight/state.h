#ifndef SKYBLIP_CORE_FLIGHT_STATE_H
#define SKYBLIP_CORE_FLIGHT_STATE_H

#include <cstdint>

namespace skyblip::flight {

// INFO: fc 18sep26 ADS-L 4 SRD860 issue 2 G.1.2 codes, the wire values themselves
enum class FlightState : uint8_t { Unknown = 0, OnGround = 1, Airborne = 2 };

struct FlightSample {
    int32_t speed_mm_s{0};
    uint16_t hdop_e2{0};  // hundredths; zero means the receiver did not report
    bool fix_valid{false};
};

constexpr int32_t kFlightSpeedMmS = 12000;
constexpr int32_t kLandingSpeedMmS = 8000;
constexpr int32_t kTaxiSpeedMmS = 1500;
constexpr int32_t kGroundSpeedMmS = 1000;
constexpr uint32_t kLandingHoldMs = 10000;
constexpr uint16_t kDopUnityE2 = 100;

// INFO: fc 18sep26 moshe-braner's jerk gate: a speed jumping 4x between solutions is noise
constexpr int32_t kJerkSpeedRatio = 4;

bool flight_evidence(const FlightSample& sample);
bool ground_evidence(const FlightSample& sample);
bool taxi_evidence(const FlightSample& sample);

FlightState state_from(uint8_t adsl_code);
inline bool airborne(uint8_t adsl_code) { return state_from(adsl_code) == FlightState::Airborne; }

// INFO: fc 23sep26 G.1.2's Undefined: no ground speed shows that a craft able to hover has landed
uint8_t announced_state(uint8_t adsl_code, uint8_t aircraft_cat);
inline bool reduced_rate(uint8_t announced) {
    return state_from(announced) == FlightState::OnGround;
}

class FlightMonitor {
   public:
    FlightState update(const FlightSample& sample, uint32_t now_ms);

    FlightState state() const { return state_; }
    bool airborne() const { return state_ == FlightState::Airborne; }
    bool rolling() const { return rolling_; }

   private:
    static bool jerky(int32_t previous_mm_s, int32_t now_mm_s);
    void update_rolling(int32_t speed_mm_s);
    void update_slowdown(const FlightSample& sample, uint32_t now_ms);
    bool taxi_sustained(uint32_t now_ms) const;

    FlightState state_{FlightState::Unknown};
    int32_t last_speed_mm_s_{0};
    uint32_t slow_since_ms_{0};
    bool slow_{false};
    bool armed_{false};
    bool rolling_{false};
};

}  // namespace skyblip::flight

#endif
