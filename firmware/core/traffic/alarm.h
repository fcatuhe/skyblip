#ifndef SKYBLIP_CORE_TRAFFIC_ALARM_H
#define SKYBLIP_CORE_TRAFFIC_ALARM_H

#include <array>
#include <cstdint>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"

namespace skyblip::traffic {

enum class Level : uint8_t { None = 0, Advisory = 1 };

constexpr uint8_t to_number(Level level) { return static_cast<uint8_t>(level); }

struct AlarmAssessment {
    Level level{Level::None};
    uint16_t relative_bearing_deg{0};
    int32_t relative_distance_m{0};
    int32_t relative_altitude_m{0};
    int32_t closing_mps{0};
    bool valid{false};
};

constexpr int32_t kAdvisoryAltitudeM = 300;
constexpr int32_t kAdvisoryDistanceM = 3000;

// INFO: fc 14sep26 a relayed target arrives with no velocity, and zero would make it the safest dot
constexpr int32_t kUnknownTargetSpeedMps = 30;

// INFO: al 02aug26 SoftRF alerts only on targets seen within ALERT_EXPIRATION_TIME
// (5 s) and re-checks no more often than every 2 s
// (oss/SoftRF-lyusupov .../src/TrafficHelper.h:58-59, .../src/TrafficHelper.cpp:236-260).
constexpr uint32_t kAlertMaxAgeMs = 5000;
constexpr uint32_t kRenotifyFloorMs = 2000;
// INFO: fc 11oct26 a touch-and-go or a stop-and-go is back in the air well inside a minute
constexpr uint32_t kGroundRenotifyFloorMs = 60000;

constexpr int kTrackedTargets = 48;

int32_t closing_mm_s(const model::Ownship& own, const model::AircraftObservation& target,
                     int32_t north_m, int32_t east_m, int32_t distance_m);

// INFO: fc 13sep26 now_ms is the instant both sides are carried to, on ports::Clock
AlarmAssessment assess(const model::Ownship& own_fix, const model::AircraftObservation& reported,
                       uint32_t now_ms);

class AlarmTracker {
   public:
    struct Decision {
        AlarmAssessment assessment{};
        bool notify{false};
        bool dismissed{false};
    };

    Decision update(const model::Ownship& own, const model::AircraftObservation& target,
                    uint32_t now_ms);

    void forget_stale(uint32_t now_ms);
    void withdraw(uint8_t address_mapping_table, uint32_t address);

    void dismiss();
    bool dismissed() const;

    Level announced_level(uint32_t now_ms) const;

   private:
    struct Slot {
        bool used{false};
        uint8_t address_mapping_table{0};
        uint32_t address{0};
        uint32_t reception_key{0};
        uint32_t seen_ms{0};
        uint32_t forget_ms{0};
        Level notified_level{Level::None};
        bool dismissed{false};
        bool falling{false};
        uint32_t falling_since_ms{0};
    };

    Slot* slot_for(const model::AircraftObservation& target, uint32_t now_ms);
    static bool notify_for(Slot& slot, Level level, uint32_t floor_ms, uint32_t now_ms);

    std::array<Slot, kTrackedTargets> slots_{};
};

}  // namespace skyblip::traffic

#endif
