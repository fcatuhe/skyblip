#include "core/traffic/table.h"

#include "core/flight/state.h"
#include "core/flight/turn.h"
#include "core/model/aircraft.h"
#include "core/settings/address.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/versioning.h"

namespace skyblip::traffic {

namespace {
uint32_t observation_at_s(const model::AircraftObservation& observation) {
    return observation.received.at_s;
}
// INFO: fc 12nov26 our second lags our clock by one, a paired Bravo's runs one ahead: no further
constexpr int32_t kReceptionAheadCeilingS = 2;

uint32_t age_s(const model::AircraftObservation& observation, uint32_t now_s) {
    const int32_t age = static_cast<int32_t>(now_s - observation_at_s(observation));
    if (age >= 0) return static_cast<uint32_t>(age);
    return age >= -kReceptionAheadCeilingS ? 0u : UINT32_MAX;
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
    return own_icao_ != 0 && observation.address_mapping_table == adsl::kAddressTableIcao &&
           address == own_icao_;
}

void TrafficTable::set_own_icao(uint32_t address) {
    const uint32_t masked = address & settings::kAddressMask;
    if (masked == own_icao_) return;
    own_icao_ = masked;
    const int index = own_icao_ == 0 ? -1 : find(adsl::kAddressTableIcao, own_icao_);
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
                  source_rank(observation.source), age_s(observation, now_s)};
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
    if (own_aircraft(observation)) return -1;
    if (observation.source == model::Source::Adsb && !observation.position_valid) {
        adsb_without_position_++;
        return -1;
    }
    // One observation at a time, deliberately: an uplink frame carries up to
    // ten aircraft and one implausible entry among them says nothing about
    // the other nine, so a ghost is refused without taking a good report with
    // it. A refused observation also never displaces the slot an aircraft of that
    // address already holds - the refusal happens before find().
    int32_t slant_m = 0;
    if (range_check(own_, observation, slant_m) == Plausibility::TooFar) {
        implausible_++;
        return -1;
    }
    int index = find(observation.address_mapping_table, observation.address);
    if (index >= 0) {
        Target& held = slots_[index];
        note_version(held.version, observation);
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
    slots_[index].version = PeerVersion{};
    note_version(slots_[index].version, observation);
    slots_[index].liftoff = flight::Liftoff{};
    sample_turn(slots_[index].turn, observation);
    slots_[index].alarm_level = Level::None;
    slots_[index].alarm_dismissed = false;
    slots_[index].in_formation = false;
    return index;
}

void TrafficTable::note_version(PeerVersion& version,
                                const model::AircraftObservation& observation) {
    if (observation.source != model::Source::AdslDirect) return;
    version.adsl = true;
    if (!version.declared) version.max = observation.adsl_version;
}

bool TrafficTable::declare_max_version(uint8_t address_mapping_table, uint32_t address,
                                       uint8_t version) {
    const int index = find(address_mapping_table, address);
    if (index < 0) return false;
    slots_[index].version.adsl = true;
    slots_[index].version.declared = true;
    slots_[index].version.max = version;
    return true;
}

// INFO: fc 13nov26 a neighbour with no valid height counts as level with us, the cautious reading
uint8_t TrafficTable::readable_version(const model::Ownship& own, uint32_t now_ms) const {
    uint8_t version = adsl::Packet::kMaxProtocolVersion;
    for (const Target& target : slots_) {
        if (!target.used || !target.version.adsl || target.version.max >= version) continue;
        const AlarmAssessment seen = assess(own, target.observation, now_ms);
        if (!seen.valid) continue;
        const int32_t vertical_m = seen.relative_altitude_valid ? seen.relative_altitude_m : 0;
        if (adsl::close_client(seen.relative_distance_m, vertical_m, seen.closing_mm_s))
            version = target.version.max;
    }
    return version;
}

void TrafficTable::watch_liftoff(Target& target, const model::AircraftObservation& incoming) {
    if (flight::on_ground(target.observation.flight_state) &&
        flight::airborne(incoming.flight_state))
        target.liftoff = {incoming.at_ms, true};
}

void TrafficTable::age_out(uint32_t now_s) {
    for (int i = 0; i < kMaxTargets; i++) {
        if (!slots_[i].used) continue;
        if (age_s(slots_[i].observation, now_s) > forget_s(slots_[i].observation))
            release(slots_[i]);
    }
}

int TrafficTable::count() const {
    int count = 0;
    for (int i = 0; i < kMaxTargets; i++)
        if (slots_[i].used) count++;
    return count;
}

void TrafficTable::clear() {
    for (auto& slot : slots_) release(slot);
}

void TrafficTable::release(Target& target) {
    target.used = false;
    target.turn = TargetTurn{};
    target.version = PeerVersion{};
    target.alarm_level = Level::None;
    target.alarm_dismissed = false;
    target.in_formation = false;
}

}
