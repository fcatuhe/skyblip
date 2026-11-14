#ifndef SKYBLIP_CORE_FLIGHT_STATE_H
#define SKYBLIP_CORE_FLIGHT_STATE_H

#include <cstdint>

#include "protocols/adsl/flight_state.h"

namespace skyblip::flight {

// INFO: fc 13nov26 skyBlip's flight state is the ADS-L G.1.2 code it sends and reads
using adsl::FlightState;
using adsl::state_from;

struct FlightSample {
    int32_t speed_mm_s{0};
    uint16_t hdop_e2{0};  // hundredths; zero means the receiver did not report
    bool fix_valid{false};
};

constexpr int32_t kFlightSpeedMmS = 12000;
constexpr int32_t kLandingSpeedMmS = 8000;
constexpr int32_t kRollingSpeedMmS = 1500;
constexpr int32_t kGroundSpeedMmS = 1000;
constexpr uint32_t kLandingHoldMs = 10000;
constexpr uint16_t kDopUnityE2 = 100;

// INFO: fc 18sep26 moshe-braner's jerk gate: a speed jumping 4x between solutions is noise
constexpr int32_t kJerkSpeedRatio = 4;

struct Liftoff {
    uint32_t at_ms{0};
    bool valid{false};
};

bool flight_evidence(const FlightSample& sample);
bool ground_evidence(const FlightSample& sample);
bool taxi_evidence(const FlightSample& sample);

inline bool airborne(uint8_t adsl_code) { return state_from(adsl_code) == FlightState::Airborne; }
inline bool on_ground(uint8_t adsl_code) { return state_from(adsl_code) == FlightState::Ground; }

class FlightMonitor {
   public:
    FlightState update(const FlightSample& sample, uint32_t now_ms);

    FlightState state() const { return state_; }
    bool airborne() const { return state_ == FlightState::Airborne; }
    bool rolling() const { return rolling_; }

   private:
    static bool jerky(int32_t previous_mm_s, int32_t now_mm_s);
    void update_rolling(int32_t speed_mm_s);
    void update_taxi(const FlightSample& sample, uint32_t now_ms);
    bool taxi_sustained(uint32_t now_ms) const;

    FlightState state_{FlightState::Unknown};
    int32_t last_speed_mm_s_{0};
    uint32_t taxiing_since_ms_{0};
    bool taxiing_{false};
    bool previous_speed_valid_{false};
    bool rolling_{false};
};

}  // namespace skyblip::flight

#endif
