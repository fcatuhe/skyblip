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
constexpr int32_t kVertM = 100;
constexpr int32_t kDriftM = 60;
constexpr int32_t kVertDriftM = 30;
constexpr uint32_t kTogetherHoldMs = 6000;
constexpr int kBreakFixes = 2;
constexpr int kTrackedContacts = 8;
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
    void observe_own(const model::OwnState& own, uint32_t now_ms);
    Report observe(const model::OwnState& own_fix, const model::AircraftObs& reported,
                   const flight::Liftoff& contact_liftoff, uint32_t now_ms);

    bool together(uint8_t addr_table, uint32_t addr) const;
    int members() const;

    void forget_stale(uint32_t now_ms);

   private:
    struct Slot {
        bool used{false};
        bool anchored{false};
        uint8_t addr_table{0};
        uint32_t addr{0};
        uint32_t seen_ms{0};
        uint32_t obs_key{0};
        int32_t ref_ahead_m{0};
        int32_t ref_right_m{0};
        int32_t ref_up_m{0};
        uint32_t steady_since_ms{0};
        int drift_fixes{0};
        int closing_fixes{0};
        State state{State::None};
    };

    static void anchor(Slot& slot, const Report& station, uint32_t now_ms);
    static void off_station(Slot& slot, const Report& fix, uint32_t now_ms);
    static bool closure_confirmed(Slot& slot, int32_t closing_mm_s);
    bool lifting(const flight::Liftoff& contact_liftoff, uint32_t now_ms) const;
    void drop(uint8_t addr_table, uint32_t addr);
    Slot* slot_for(const model::AircraftObs& target, uint32_t now_ms);
    const Slot* find(uint8_t addr_table, uint32_t addr) const;
    Slot* find(uint8_t addr_table, uint32_t addr);

    std::array<Slot, kTrackedContacts> slots_{};
    flight::Liftoff own_liftoff_{};
    bool own_grounded_{false};
};

}  // namespace skyblip::formation

#endif
