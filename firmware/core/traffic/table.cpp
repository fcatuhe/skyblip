#include "core/traffic/table.h"

#include "core/flight/state.h"
#include "core/flight/turn.h"
#include "core/model/aircraft.h"
#include "core/settings/address.h"
#include "protocols/adsl/packet.h"
#include "protocols/common/geometry.h"

namespace skyblip::traffic {

namespace {
uint32_t observation_at_s(const model::AircraftObservation& observation) {
    return observation.received.at_s;
}
int source_rank(model::Source source) {
    switch (source) {
        case model::Source::AdslDirect:
        case model::Source::Alptas: return 3;
        case model::Source::Adsb: return 2;
        case model::Source::AdslUplink: return 1;
        default: return 0;
    }
}
}

bool TrafficTable::own_aircraft(const model::AircraftObservation& observation) const {
    const uint32_t address = observation.address & settings::kAddressMask;
    if (own_address_ != 0 && observation.address_mapping_table == own_address_mapping_table_ &&
        address == own_address_)
        return true;
    return aircraft_address_ != 0 && observation.address_mapping_table == adsl::kAddressTableIcao &&
           address == aircraft_address_;
}

void TrafficTable::set_aircraft_address(uint32_t address) {
    const uint32_t masked = address & settings::kAddressMask;
    if (masked == aircraft_address_) return;
    aircraft_address_ = masked;
    const int index =
        aircraft_address_ == 0 ? -1 : find(adsl::kAddressTableIcao, aircraft_address_);
    if (index >= 0) release(slots_[index]);
}

bool TrafficTable::own_flarm(const model::AircraftObservation& observation) const {
    return flarm_id_ != 0 && observation.address_mapping_table == adsl::kAddressTableFlarm &&
           (observation.address & settings::kAddressMask) == flarm_id_;
}

void TrafficTable::set_flarm_id(uint32_t flarm_id) {
    const uint32_t masked = flarm_id & settings::kAddressMask;
    if (masked == flarm_id_) return;
    flarm_id_ = masked;
    const int index = flarm_id_ == 0 ? -1 : find(adsl::kAddressTableFlarm, flarm_id_);
    if (index >= 0) release(slots_[index]);
}

int TrafficTable::find(uint8_t address_mapping_table, uint32_t address) const {
    for (int i = 0; i < kMaxTargets; i++) {
        if (slots_[i].used && slots_[i].observation.address == address &&
            slots_[i].observation.address_mapping_table == address_mapping_table)
            return i;
    }
    return -1;
}

bool TrafficTable::prefer_new(const model::AircraftObservation& incoming,
                              const model::AircraftObservation& existing) {
    const uint32_t incoming_s = observation_at_s(incoming), existing_s = observation_at_s(existing);
    const int incoming_rank = source_rank(incoming.source),
              existing_rank = source_rank(existing.source);
    // INFO: fc 20nov26 a relay is newer than the direct report it repeats, so newer is not enough
    if (incoming_rank < existing_rank && incoming_s >= existing_s &&
        incoming_s - existing_s <= direct_preferred_max_age_s(existing))
        return false;
    if (incoming_s != existing_s) return incoming_s > existing_s;
    return incoming_rank >= existing_rank;
}

void TrafficTable::sample_turn(TargetTurn& turn, const model::AircraftObservation& observation) {
    if (!observation.speed_valid) return;
    if (observation.turn_valid) {
        turn.armed = true;
        turn.valid = true;
        turn.rate_dps =
            static_cast<int16_t>(wire::divide_rounded<int32_t>(observation.turn_cdps, 100));
        turn.reference_at_ms = observation.at_ms;
        turn.reference_track_c9 = observation.track_c9;
        return;
    }
    const uint32_t elapsed_ms = observation.at_ms - turn.reference_at_ms;
    if (!turn.armed || elapsed_ms > kTurnMaxAgeMs) {
        turn.armed = true;
        turn.valid = false;
        turn.rate_dps = 0;
        turn.reference_at_ms = observation.at_ms;
        turn.reference_track_c9 = observation.track_c9;
        return;
    }
    if (elapsed_ms < flight::kTurnWindowMs) return;
    turn.rate_dps =
        flight::turn_rate_dps(observation.track_c9, turn.reference_track_c9, elapsed_ms);
    turn.valid = true;
    turn.reference_at_ms = observation.at_ms;
    turn.reference_track_c9 = observation.track_c9;
}

// INFO: fc 23sep26 a full table keeps the nearest: a flood of far or relayed frames evicts none
bool TrafficTable::matters_less(const Weight& weight, const Weight& other) {
    if (weight.on_ground != other.on_ground) return weight.on_ground;
    if (weight.slant_m != other.slant_m) return weight.slant_m > other.slant_m;
    if (weight.rank != other.rank) return weight.rank < other.rank;
    return weight.age_s > other.age_s;
}

TrafficTable::Weight TrafficTable::weight_of(const model::AircraftObservation& observation,
                                             uint32_t now_s) const {
    int32_t slant_m = 0;
    if (range_check(own_, observation, slant_m) == Plausibility::NoReference) slant_m = 0;
    return Weight{flight::on_ground(observation.flight_state), slant_m,
                  source_rank(observation.source), age_s(observation_at_s(observation), now_s)};
}

int TrafficTable::allocate_slot(const model::AircraftObservation& incoming, uint32_t now_s) {
    for (int i = 0; i < kMaxTargets; i++)
        if (!slots_[i].used) return i;
    int victim = -1;
    Weight least{};
    for (int i = 0; i < kMaxTargets; i++) {
        if (slots_[i].alarm_level != Level::None) continue;
        const Weight weight = weight_of(slots_[i].observation, now_s);
        if (victim < 0 || matters_less(weight, least)) {
            victim = i;
            least = weight;
        }
    }
    if (victim >= 0 && !matters_less(least, weight_of(incoming, now_s))) return -1;
    return victim;
}

int TrafficTable::update(const model::AircraftObservation& observation, uint32_t now_s) {
    if (own_aircraft(observation) || own_flarm(observation)) return -1;
    if (observation.source == model::Source::Adsb && !observation.position_valid) {
        adsb_without_position_++;
        return -1;
    }
    // INFO: fc 20nov26 refuse before find(), so a ghost never displaces the slot its address holds
    int32_t slant_m = 0;
    if (range_check(own_, observation, slant_m) == Plausibility::TooFar) {
        implausible_++;
        return -1;
    }
    int index = find(observation.address_mapping_table, observation.address);
    if (index >= 0) {
        Target& held = slots_[index];
        if (!prefer_new(observation, held.observation)) return index;
        watch_liftoff(held, observation);
        held.observation = observation;
        held.revision = ++revisions_;
        sample_turn(held.turn, observation);
        return index;
    }
    index = allocate_slot(observation, now_s);
    if (index < 0) return -1;
    slots_[index].used = true;
    slots_[index].observation = observation;
    slots_[index].revision = ++revisions_;
    slots_[index].turn = TargetTurn{};
    slots_[index].liftoff = flight::Liftoff{};
    sample_turn(slots_[index].turn, observation);
    slots_[index].alarm_level = Level::None;
    slots_[index].alarm_dismissed = false;
    slots_[index].in_formation = false;
    return index;
}

void TrafficTable::watch_liftoff(Target& target, const model::AircraftObservation& incoming) {
    if (flight::on_ground(target.observation.flight_state) &&
        flight::airborne(incoming.flight_state))
        target.liftoff = {incoming.at_ms, true};
}

void TrafficTable::age_out(uint32_t now_s) {
    for (int i = 0; i < kMaxTargets; i++) {
        if (!slots_[i].used) continue;
        if (age_s(observation_at_s(slots_[i].observation), now_s) > forget_s(slots_[i].observation))
            release(slots_[i]);
    }
}

int TrafficTable::count() const {
    int count = 0;
    for (int i = 0; i < kMaxTargets; i++)
        if (slots_[i].used) count++;
    return count;
}

int TrafficTable::count_of(model::Source source) const {
    int count = 0;
    for (int i = 0; i < kMaxTargets; i++)
        if (slots_[i].used && slots_[i].observation.source == source) count++;
    return count;
}

void TrafficTable::release(Target& target) {
    target.used = false;
    target.turn = TargetTurn{};
    target.alarm_level = Level::None;
    target.alarm_dismissed = false;
    target.in_formation = false;
}

}
