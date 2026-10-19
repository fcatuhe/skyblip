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

    // Our own 24-bit address. A ground station relays every aircraft it heard,
    // and it heard us: without this the uplink puts own-ship on the radar, at
    // own-ship's position, and the alarm layer grades a head-on with the
    // aircraft it is bolted to. Zero means nothing is filtered, which is what a
    // table nobody told has to assume.
    void set_own_address(uint8_t address_mapping_table, uint32_t address) {
        own_address_mapping_table_ = address_mapping_table;
        own_address_ = address & settings::kAddressMask;
    }

    // Where own-ship is, for the range gate below. This is the table's door and
    // three paths come through it - a direct ADS-L frame, an ALP-TAS frame and up
    // to thirteen aircraft relayed inside one uplink frame - so the gate lives
    // here rather than on any one of them: a path added later is gated by
    // construction instead of by remembering. Whoever drains the radio bus
    // refreshes this each pass; a table nobody told has no reference and gates
    // nothing, which is also what a device without a fix has.
    void set_own_reference(const model::Ownship& own) { own_ = own; }

    // Receptions refused because the position they claimed was further away than
    // this radio can hear (core/traffic/sanity.h). Not silent: a rate that climbs
    // is a receiver at the edge of its budget or a decoder that is wrong.
    uint32_t implausible() const { return implausible_; }

    int update(const model::AircraftObservation& observation, uint32_t now_s);

    void age_out(uint32_t now_s);

    int count() const;
    const Target* at(int i) const { return (i >= 0 && i < kMaxTargets) ? &slots_[i] : nullptr; }
    Target* at(int i) { return (i >= 0 && i < kMaxTargets) ? &slots_[i] : nullptr; }
    void clear();

    int find(uint8_t address_mapping_table, uint32_t address) const;

   private:
    std::array<Target, kMaxTargets> slots_{};
    model::Ownship own_{};
    uint32_t own_address_{0};
    uint8_t own_address_mapping_table_{0};
    uint32_t implausible_{0};

    struct Weight {
        bool on_ground;
        int32_t slant_m;
        int rank;
        uint32_t age_s;
    };

    static bool prefer_new(const model::AircraftObservation& incoming,
                           const model::AircraftObservation& existing);
    static void sample_turn(TargetTurn& turn, const model::AircraftObservation& observation);
    static void watch_liftoff(Target& target, const model::AircraftObservation& incoming);
    static bool matters_less(const Weight& a, const Weight& b);
    Weight weight_of(const model::AircraftObservation& observation, uint32_t now_s) const;
    int allocate_slot(const model::AircraftObservation& incoming, uint32_t now_s);
};

}

#endif
