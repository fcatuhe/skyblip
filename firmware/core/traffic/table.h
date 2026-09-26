#ifndef SKYBLIP_CORE_TRAFFIC_TABLE_H
#define SKYBLIP_CORE_TRAFFIC_TABLE_H

#include <array>
#include <cstdint>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/traffic/alarm.h"
#include "core/traffic/lease.h"
#include "core/traffic/sanity.h"

namespace skyblip::traffic {

struct TargetTurn {
    int16_t dps{0};
    bool valid{false};
    bool armed{false};
    uint32_t ref_ms{0};
    uint16_t ref_track_c9{0};
};

struct Target {
    model::AircraftObs obs;
    TargetTurn turn;
    Level alarm_level{Level::None};
    bool alarm_dismissed{false};
    bool in_formation{false};
    bool used{false};
};

constexpr uint32_t kTurnMaxGapMs = 3000;

class TrafficTable {
   public:
    static constexpr int kCapacity = 48;

    // Our own 24-bit address. A ground station relays every aircraft it heard,
    // and it heard us: without this the uplink puts own-ship on the radar, at
    // own-ship's position, and the alarm layer grades a head-on with the
    // aircraft it is bolted to. Zero means nothing is filtered, which is what a
    // table nobody told has to assume.
    void set_own_address(uint8_t addr_table, uint32_t addr) {
        own_addr_table_ = addr_table;
        own_addr_ = addr & 0x00FFFFFF;
    }

    // Where own-ship is, for the range gate below. This is the table's door and
    // three paths come through it - a direct ADS-L frame, an ALP-TAS frame and up
    // to thirteen aircraft relayed inside one uplink frame - so the gate lives
    // here rather than on any one of them: a path added later is gated by
    // construction instead of by remembering. Whoever drains the radio bus
    // refreshes this each pass; a table nobody told has no reference and gates
    // nothing, which is also what a device without a fix has.
    void set_own_reference(const model::OwnState& own) { own_ = own; }

    // Receptions refused because the position they claimed was further away than
    // this radio can hear (core/traffic/sanity.h). Not silent: a rate that climbs
    // is a receiver at the edge of its budget or a decoder that is wrong.
    uint32_t implausible_count() const { return implausible_; }

    int update(const model::AircraftObs& obs, uint32_t now);

    void age_out(uint32_t now);

    int count() const;
    const Target* at(int i) const { return (i >= 0 && i < kCapacity) ? &slots_[i] : nullptr; }
    Target* at(int i) { return (i >= 0 && i < kCapacity) ? &slots_[i] : nullptr; }
    void clear();

    int find(uint8_t addr_table, uint32_t addr) const;

   private:
    std::array<Target, kCapacity> slots_{};
    model::OwnState own_{};
    uint32_t own_addr_{0};
    uint8_t own_addr_table_{0};
    uint32_t implausible_{0};

    struct Weight {
        int32_t slant_m;
        int rank;
        uint32_t age_s;
    };

    static bool prefer_new(const model::AircraftObs& incoming, const model::AircraftObs& existing);
    static void sample_turn(TargetTurn& turn, const model::AircraftObs& obs);
    static bool matters_less(const Weight& a, const Weight& b);
    Weight weight_of(const model::AircraftObs& obs, uint32_t now) const;
    int allocate_slot(const model::AircraftObs& incoming, uint32_t now);
};

}

#endif
