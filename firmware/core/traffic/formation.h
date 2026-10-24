#ifndef SKYBLIP_CORE_TRAFFIC_FORMATION_H
#define SKYBLIP_CORE_TRAFFIC_FORMATION_H

#include <array>
#include <cstdint>

#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"

namespace skyblip::formation {

enum class State : uint8_t { None, Joining, Together, Parting };

constexpr int32_t kRangeM = 1000;
constexpr int32_t kVerticalM = 100;
constexpr int32_t kDriftM = 60;
constexpr int32_t kVerticalDriftM = 30;
constexpr uint32_t kTogetherHoldMs = 6000;
constexpr int kBreakFixes = 2;
constexpr int kMaxTrackedTargets = 8;
constexpr int32_t kClosingMps = 3;
constexpr int kClosingFixes = 2;
constexpr uint32_t kLiftoffWindowMs = kTogetherHoldMs;

struct Report {
    State state{State::None};
    int32_t ahead_m{0};
    int32_t right_m{0};
    int32_t up_m{0};
    bool valid{false};
};

class Tracker {
   public:
    void observe_own(const model::Ownship& own, uint32_t now_ms);
    Report observe(const model::Ownship& own_fix, const model::AircraftObservation& reported,
                   const flight::Liftoff& target_liftoff, uint32_t now_ms);

    bool together(uint8_t address_mapping_table, uint32_t address) const;
    int members() const;

    void forget_stale(uint32_t now_ms);

   private:
    struct Slot {
        bool used{false};
        bool anchored{false};
        uint8_t address_mapping_table{0};
        uint32_t address{0};
        uint32_t seen_ms{0};
        uint32_t reception_key{0};
        int32_t anchor_ahead_m{0};
        int32_t anchor_right_m{0};
        int32_t anchor_up_m{0};
        uint32_t on_station_since_ms{0};
        int drift_fixes{0};
        int closing_fixes{0};
        State state{State::None};
    };

    static void anchor(Slot& slot, const Report& station, uint32_t now_ms);
    static void off_station(Slot& slot, const Report& fix, uint32_t now_ms);
    static bool closure_confirmed(Slot& slot, int32_t closing_mm_s);
    bool lifting(const flight::Liftoff& target_liftoff, uint32_t now_ms) const;
    void drop(uint8_t address_mapping_table, uint32_t address);
    Slot* slot_for(const model::AircraftObservation& target, uint32_t now_ms);
    const Slot* find(uint8_t address_mapping_table, uint32_t address) const;
    Slot* find(uint8_t address_mapping_table, uint32_t address);

    std::array<Slot, kMaxTrackedTargets> slots_{};
    flight::Liftoff own_liftoff_{};
    bool own_grounded_{false};
};

}  // namespace skyblip::formation

#endif
