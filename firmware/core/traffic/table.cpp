#include "core/traffic/table.h"

#include "core/flight/state.h"
#include "core/flight/turn.h"
#include "core/model/aircraft.h"
#include "core/settings/address.h"

namespace skyblip::traffic {

namespace {
uint32_t observation_at_s(const model::AircraftObservation& o) { return o.received.at_s; }
int source_rank(model::Source s) {
    switch (s) {
        case model::Source::AdslDirect:
        case model::Source::Alptas: return 3;
        case model::Source::AdslUplink: return 1;
        default: return 0;
    }
}
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
    // A ground relay never displaces a first-hand reception the alarm layer
    // would still act on. Being newer is not enough: a relayed frame is always
    // newer than the direct one it repeats, which is how a relay would
    // otherwise walk a target backwards once a second, for ever.
    if (incoming_rank < existing_rank && incoming_s >= existing_s &&
        incoming_s - existing_s <= direct_preferred_max_age_s(existing))
        return false;
    if (incoming_s != existing_s) return incoming_s > existing_s;
    return incoming_rank >= existing_rank;
}

void TrafficTable::sample_turn(TargetTurn& turn, const model::AircraftObservation& observation) {
    if (!observation.speed_valid) return;
    const uint32_t dt = observation.at_ms - turn.reference_at_ms;
    if (!turn.armed || dt > kTurnMaxAgeMs) {
        turn.armed = true;
        turn.valid = false;
        turn.rate_dps = 0;
        turn.reference_at_ms = observation.at_ms;
        turn.reference_track_c9 = observation.track_c9;
        return;
    }
    if (dt < flight::kTurnWindowMs) return;
    turn.rate_dps = flight::turn_rate_dps(observation.track_c9, turn.reference_track_c9, dt);
    turn.valid = true;
    turn.reference_at_ms = observation.at_ms;
    turn.reference_track_c9 = observation.track_c9;
}

// INFO: fc 23sep26 a full table keeps the nearest: a flood of far or relayed frames evicts none
bool TrafficTable::matters_less(const Weight& a, const Weight& b) {
    if (a.on_ground != b.on_ground) return a.on_ground;
    if (a.slant_m != b.slant_m) return a.slant_m > b.slant_m;
    if (a.rank != b.rank) return a.rank < b.rank;
    return a.age_s > b.age_s;
}

TrafficTable::Weight TrafficTable::weight_of(const model::AircraftObservation& observation,
                                             uint32_t now_s) const {
    int32_t slant_m = 0;
    if (range_check(own_, observation, slant_m) == Plausibility::NoReference) slant_m = 0;
    return Weight{flight::on_ground(observation.flight_state), slant_m,
                  source_rank(observation.source), now_s - observation_at_s(observation)};
}

int TrafficTable::allocate_slot(const model::AircraftObservation& incoming, uint32_t now_s) {
    for (int i = 0; i < kMaxTargets; i++)
        if (!slots_[i].used) return i;
    int victim = -1;
    Weight least{};
    for (int i = 0; i < kMaxTargets; i++) {
        if (slots_[i].alarm_level != Level::None) continue;
        const Weight w = weight_of(slots_[i].observation, now_s);
        if (victim < 0 || matters_less(w, least)) {
            victim = i;
            least = w;
        }
    }
    if (victim >= 0 && !matters_less(least, weight_of(incoming, now_s))) return -1;
    return victim;
}

int TrafficTable::update(const model::AircraftObservation& observation, uint32_t now_s) {
    if (own_address_ != 0 && observation.address_mapping_table == own_address_mapping_table_ &&
        (observation.address & settings::kAddressMask) == own_address_)
        return -1;
    // One observation at a time, deliberately: an uplink frame carries up to
    // thirteen aircraft and one implausible entry among them says nothing about
    // the other twelve, so a ghost is refused without taking a good report with
    // it. A refused observation also never displaces the slot an aircraft of that
    // address already holds - the refusal happens before find().
    int32_t slant_m = 0;
    if (range_check(own_, observation, slant_m) == Plausibility::TooFar) {
        implausible_++;
        return -1;
    }
    int index = find(observation.address_mapping_table, observation.address);
    if (index >= 0) {
        if (prefer_new(observation, slots_[index].observation)) {
            watch_liftoff(slots_[index], observation);
            slots_[index].observation = observation;
            sample_turn(slots_[index].turn, observation);
        }
        return index;
    }
    index = allocate_slot(observation, now_s);
    if (index < 0) return -1;
    slots_[index].used = true;
    slots_[index].observation = observation;
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
        if (now_s - observation_at_s(slots_[i].observation) > forget_s(slots_[i].observation)) {
            slots_[i].used = false;
            slots_[i].turn = TargetTurn{};
            slots_[i].alarm_level = Level::None;
            slots_[i].alarm_dismissed = false;
            slots_[i].in_formation = false;
        }
    }
}

int TrafficTable::count() const {
    int n = 0;
    for (int i = 0; i < kMaxTargets; i++)
        if (slots_[i].used) n++;
    return n;
}

void TrafficTable::clear() {
    for (auto& s : slots_) {
        s.used = false;
        s.turn = TargetTurn{};
        s.alarm_level = Level::None;
        s.alarm_dismissed = false;
        s.in_formation = false;
    }
}

}
