#include "core/traffic/table.h"

#include "core/flight/turn.h"
#include "core/model/aircraft.h"

namespace skyblip::traffic {

namespace {
uint32_t obs_time(const model::AircraftObs& o) { return o.received.at_s; }
int source_rank(model::Source s) {
    switch (s) {
        case model::Source::AdslDirect:
        case model::Source::Alptas: return 3;
        case model::Source::AdslUplink: return 1;
        default: return 0;
    }
}
}

int TrafficTable::find(uint8_t addr_table, uint32_t addr) const {
    for (int i = 0; i < kCapacity; i++) {
        if (slots_[i].used && slots_[i].obs.addr == addr && slots_[i].obs.addr_table == addr_table)
            return i;
    }
    return -1;
}

bool TrafficTable::prefer_new(const model::AircraftObs& in, const model::AircraftObs& ex) {
    const uint32_t tin = obs_time(in), tex = obs_time(ex);
    const int rank_in = source_rank(in.source), rank_ex = source_rank(ex.source);
    // A ground relay never displaces a first-hand reception the alarm layer
    // would still act on. Being newer is not enough: a relayed frame is always
    // newer than the direct one it repeats, which is how a relay would
    // otherwise walk a target backwards once a second, for ever.
    if (rank_in < rank_ex && tin >= tex && tin - tex <= kDirectPreferredMaxAgeSec) return false;
    if (tin != tex) return tin > tex;
    return rank_in >= rank_ex;
}

void TrafficTable::sample_turn(TargetTurn& turn, const model::AircraftObs& obs) {
    if (!obs.speed_valid) return;
    const uint32_t dt = obs.at_ms - turn.ref_ms;
    if (!turn.armed || dt > kTurnMaxGapMs) {
        turn.armed = true;
        turn.valid = false;
        turn.dps = 0;
        turn.ref_ms = obs.at_ms;
        turn.ref_track_c9 = obs.track_c9;
        return;
    }
    if (dt < flight::kTurnWindowMs) return;
    turn.dps = flight::turn_rate_dps(obs.track_c9, turn.ref_track_c9, dt);
    turn.valid = true;
    turn.ref_ms = obs.at_ms;
    turn.ref_track_c9 = obs.track_c9;
}

// INFO: fc 23sep26 a full table keeps the nearest: a flood of far or relayed frames evicts none
bool TrafficTable::matters_less(const Weight& a, const Weight& b) {
    if (a.slant_m != b.slant_m) return a.slant_m > b.slant_m;
    if (a.rank != b.rank) return a.rank < b.rank;
    return a.age_s > b.age_s;
}

TrafficTable::Weight TrafficTable::weight_of(const model::AircraftObs& obs, uint32_t now) const {
    int32_t slant_m = 0;
    if (range_check(own_, obs, slant_m) == Plausibility::NoReference) slant_m = 0;
    return Weight{slant_m, source_rank(obs.source), now - obs_time(obs)};
}

int TrafficTable::allocate_slot(const model::AircraftObs& incoming, uint32_t now) {
    for (int i = 0; i < kCapacity; i++)
        if (!slots_[i].used) return i;
    int victim = -1;
    Weight least{};
    for (int i = 0; i < kCapacity; i++) {
        if (slots_[i].alarm_level != Level::None) continue;
        const Weight w = weight_of(slots_[i].obs, now);
        if (victim < 0 || matters_less(w, least)) {
            victim = i;
            least = w;
        }
    }
    if (victim >= 0 && !matters_less(least, weight_of(incoming, now))) return -1;
    return victim;
}

int TrafficTable::update(const model::AircraftObs& obs, uint32_t now) {
    if (own_addr_ != 0 && obs.addr_table == own_addr_table_ && (obs.addr & 0x00FFFFFF) == own_addr_)
        return -1;
    // One observation at a time, deliberately: an uplink frame carries up to
    // thirteen aircraft and one implausible entry among them says nothing about
    // the other twelve, so a ghost is refused without taking a good report with
    // it. A refused observation also never displaces the slot an aircraft of that
    // address already holds - the refusal happens before find().
    int32_t slant_m = 0;
    if (range_check(own_, obs, slant_m) == Plausibility::TooFar) {
        implausible_++;
        return -1;
    }
    int idx = find(obs.addr_table, obs.addr);
    if (idx >= 0) {
        if (prefer_new(obs, slots_[idx].obs)) {
            slots_[idx].obs = obs;
            sample_turn(slots_[idx].turn, obs);
        }
        return idx;
    }
    idx = allocate_slot(obs, now);
    if (idx < 0) return -1;
    slots_[idx].used = true;
    slots_[idx].obs = obs;
    slots_[idx].turn = TargetTurn{};
    sample_turn(slots_[idx].turn, obs);
    slots_[idx].alarm_level = Level::None;
    return idx;
}

void TrafficTable::age_out(uint32_t now, uint32_t max_age) {
    for (int i = 0; i < kCapacity; i++) {
        if (!slots_[i].used) continue;
        if (now - obs_time(slots_[i].obs) > max_age) {
            slots_[i].used = false;
            slots_[i].turn = TargetTurn{};
            slots_[i].alarm_level = Level::None;
        }
    }
}

int TrafficTable::count() const {
    int n = 0;
    for (int i = 0; i < kCapacity; i++)
        if (slots_[i].used) n++;
    return n;
}

void TrafficTable::clear() {
    for (auto& s : slots_) {
        s.used = false;
        s.turn = TargetTurn{};
        s.alarm_level = Level::None;
    }
}

}
