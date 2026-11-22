#ifndef SKYBLIP_CORE_TRAFFIC_TABLE_H
#define SKYBLIP_CORE_TRAFFIC_TABLE_H

#include <array>
#include <cstdint>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/settings/address.h"
#include "core/traffic/alarm.h"
#include "core/traffic/lease.h"
#include "core/traffic/sanity.h"

namespace skyblip::traffic {

struct TargetTurn {
    int16_t rate_dps{0};
    bool valid{false};
    bool armed{false};
    uint32_t reference_at_ms{0};
    uint16_t reference_track_c9{0};
};

struct Target {
    model::AircraftObservation observation;
    // INFO: fc 11nov26 moves each time the table takes an observation, so a reader tells news apart
    uint32_t revision{0};
    TargetTurn turn;
    flight::Liftoff liftoff;
    Level alarm_level{Level::None};
    bool alarm_dismissed{false};
    bool in_formation{false};
    bool used{false};
};

constexpr uint32_t kTurnMaxAgeMs = 3000;

class TrafficTable {
   public:
    static constexpr int kMaxTargets = 48;

    void set_own_address(uint8_t address_mapping_table, uint32_t address) {
        own_address_mapping_table_ = address_mapping_table;
        own_address_ = address & settings::kAddressMask;
    }

    // INFO: fc 11nov26 a new address releases its slot even under an alarm: that alarm was on us
    void set_aircraft_address(uint32_t address);

    void set_own_reference(const model::Ownship& own) { own_ = own; }

    uint32_t implausible() const { return implausible_; }
    uint32_t adsb_without_position() const { return adsb_without_position_; }

    int update(const model::AircraftObservation& observation, uint32_t now_s);

    void age_out(uint32_t now_s);

    int count() const;
    int count_of(model::Source source) const;
    const Target* at(int i) const { return (i >= 0 && i < kMaxTargets) ? &slots_[i] : nullptr; }
    Target* at(int i) { return (i >= 0 && i < kMaxTargets) ? &slots_[i] : nullptr; }

    int find(uint8_t address_mapping_table, uint32_t address) const;

   private:
    std::array<Target, kMaxTargets> slots_{};
    model::Ownship own_{};
    uint32_t own_address_{0};
    uint8_t own_address_mapping_table_{0};
    uint32_t aircraft_address_{0};
    uint32_t implausible_{0};
    uint32_t adsb_without_position_{0};
    uint32_t revisions_{0};

    struct Weight {
        bool on_ground;
        int32_t slant_m;
        int rank;
        uint32_t age_s;
    };

    bool own_aircraft(const model::AircraftObservation& observation) const;
    static bool prefer_new(const model::AircraftObservation& incoming,
                           const model::AircraftObservation& existing);
    static void sample_turn(TargetTurn& turn, const model::AircraftObservation& observation);
    static void release(Target& target);
    static void watch_liftoff(Target& target, const model::AircraftObservation& incoming);
    static bool matters_less(const Weight& weight, const Weight& other);
    Weight weight_of(const model::AircraftObservation& observation, uint32_t now_s) const;
    int allocate_slot(const model::AircraftObservation& incoming, uint32_t now_s);
};

}

#endif
