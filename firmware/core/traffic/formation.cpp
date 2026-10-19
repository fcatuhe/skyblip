#include "core/traffic/formation.h"

#include "core/flight/extrapolate.h"
#include "core/flight/state.h"
#include "core/protocol/nmea_out.h"
#include "core/traffic/alarm.h"
#include "core/traffic/lease.h"
#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::formation {

namespace {

constexpr int32_t kQ14One = 16384;

int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

void heading_up(int32_t north_m, int32_t east_m, int32_t track_cdeg, int32_t& ahead_m,
                int32_t& right_m) {
    const int16_t track = to_angle16(CentiDegrees(track_cdeg));
    const int64_t c = icos(track), s = isin(track);
    ahead_m = static_cast<int32_t>(div_round<int64_t>(north_m * c + east_m * s, kQ14One));
    right_m = static_cast<int32_t>(div_round<int64_t>(east_m * c - north_m * s, kQ14One));
}

uint32_t reception_key(const model::AircraftObs& obs) {
    return obs.received.at_s * kMillisecondsPerSecond + obs.received.into_ms;
}

bool recent(const flight::Liftoff& liftoff, uint32_t now_ms) {
    return liftoff.valid && now_ms - liftoff.at_ms < kLiftoffWindowMs;
}

}  // namespace

void Tracker::observe_own(const model::OwnState& own, uint32_t now_ms) {
    switch (flight::state_from(own.flight_state)) {
        case flight::FlightState::Ground:
            own_grounded_ = true;
            slots_.fill(Slot{});
            return;
        case flight::FlightState::Airborne:
            if (own_grounded_) own_liftoff_ = {now_ms, true};
            own_grounded_ = false;
            return;
        case flight::FlightState::Unknown: return;
    }
}

void Tracker::anchor(Slot& slot, const Report& station, uint32_t now_ms) {
    slot.ref_ahead_m = station.ahead_m;
    slot.ref_right_m = station.right_m;
    slot.ref_up_m = station.up_m;
    slot.steady_since_ms = now_ms;
    slot.drift_fixes = 0;
    slot.closing_fixes = 0;
    slot.anchored = true;
}

Report Tracker::observe(const model::OwnState& own_fix, const model::AircraftObs& reported,
                        const flight::Liftoff& contact_liftoff, uint32_t now_ms) {
    Report out{};
    if (flight::on_ground(own_fix.flight_state) || flight::on_ground(reported.flight_state)) {
        drop(reported.address_mapping_table, reported.address);
        return out;
    }
    const model::OwnState own = flight::carried_to(own_fix, now_ms);
    const model::AircraftObs target = flight::carried_to(reported, now_ms);
    int32_t north_m, east_m, up_m;
    if (!protocol::relative_ned(own, target, north_m, east_m, up_m)) return out;

    heading_up(north_m, east_m, own.track_cdeg, out.ahead_m, out.right_m);
    out.up_m = up_m;
    out.valid = true;

    Slot* slot = slot_for(target, now_ms);
    if (slot == nullptr) return out;
    slot->seen_ms = now_ms;

    const int32_t range_m = static_cast<int32_t>(idistance(north_m, east_m));
    if (range_m > kRangeM || iabs32(up_m) > kVertM) {
        *slot = Slot{};
        return out;
    }

    const uint32_t key = reception_key(reported);
    if (slot->anchored && key == slot->obs_key) {
        out.state = slot->state;
        return out;
    }
    slot->obs_key = key;

    if (!slot->anchored) {
        anchor(*slot, out, now_ms);
        if (lifting(contact_liftoff, now_ms)) slot->state = State::Joining;
        out.state = slot->state;
        return out;
    }

    if (closure_confirmed(*slot, traffic::closing_mm_s(own, target, north_m, east_m, range_m))) {
        anchor(*slot, out, now_ms);
        slot->state = State::None;
        return out;
    }

    const int32_t drift_m = static_cast<int32_t>(
        idistance(out.ahead_m - slot->ref_ahead_m, out.right_m - slot->ref_right_m));
    const bool station_kept =
        drift_m <= kDriftM && iabs32(out.up_m - slot->ref_up_m) <= kVertDriftM;

    if (station_kept) {
        slot->drift_fixes = 0;
        if (slot->state != State::Together && now_ms - slot->steady_since_ms >= kTogetherHoldMs)
            slot->state = State::Together;
    } else {
        off_station(*slot, out, now_ms);
    }
    out.state = slot->state;
    return out;
}

bool Tracker::lifting(const flight::Liftoff& contact_liftoff, uint32_t now_ms) const {
    return recent(own_liftoff_, now_ms) || recent(contact_liftoff, now_ms);
}

bool Tracker::closure_confirmed(Slot& slot, int32_t closing_mm_s) {
    if (slot.state == State::None || closing_mm_s < kClosingMps * kMillimetresPerMetre) {
        slot.closing_fixes = 0;
        return false;
    }
    return ++slot.closing_fixes >= kClosingFixes;
}

void Tracker::off_station(Slot& slot, const Report& fix, uint32_t now_ms) {
    slot.steady_since_ms = now_ms;
    if (slot.state == State::None || slot.state == State::Joining) {
        anchor(slot, fix, now_ms);
        slot.state = State::None;
    } else if (slot.state == State::Together && ++slot.drift_fixes >= kBreakFixes) {
        slot.state = State::Parting;
    }
}

bool Tracker::together(uint8_t address_mapping_table, uint32_t address) const {
    const Slot* slot = find(address_mapping_table, address);
    return slot != nullptr && slot->state == State::Together;
}

int Tracker::members() const {
    int n = 0;
    for (const Slot& s : slots_)
        if (s.used && s.state == State::Together) n++;
    return n;
}

void Tracker::forget_stale(uint32_t now_ms) {
    constexpr uint32_t lease_ms = traffic::kAirborneTargetForgetS * kMillisecondsPerSecond;
    for (Slot& s : slots_)
        if (s.used && now_ms - s.seen_ms > lease_ms) s = Slot{};
}

void Tracker::drop(uint8_t address_mapping_table, uint32_t address) {
    Slot* slot = find(address_mapping_table, address);
    if (slot != nullptr) *slot = Slot{};
}

Tracker::Slot* Tracker::slot_for(const model::AircraftObs& target, uint32_t now_ms) {
    Slot* free_slot = nullptr;
    Slot* oldest = nullptr;
    for (Slot& s : slots_) {
        if (s.used && s.address == target.address &&
            s.address_mapping_table == target.address_mapping_table)
            return &s;
        if (!s.used) {
            if (free_slot == nullptr) free_slot = &s;
            continue;
        }
        if (s.state == State::Together) continue;
        if (oldest == nullptr || now_ms - s.seen_ms > now_ms - oldest->seen_ms) oldest = &s;
    }
    Slot* slot = free_slot != nullptr ? free_slot : oldest;
    if (slot == nullptr) return nullptr;
    *slot = Slot{};
    slot->used = true;
    slot->address = target.address;
    slot->address_mapping_table = target.address_mapping_table;
    slot->seen_ms = now_ms;
    return slot;
}

const Tracker::Slot* Tracker::find(uint8_t address_mapping_table, uint32_t address) const {
    for (const Slot& s : slots_)
        if (s.used && s.address == address && s.address_mapping_table == address_mapping_table)
            return &s;
    return nullptr;
}

Tracker::Slot* Tracker::find(uint8_t address_mapping_table, uint32_t address) {
    for (Slot& s : slots_)
        if (s.used && s.address == address && s.address_mapping_table == address_mapping_table)
            return &s;
    return nullptr;
}

}  // namespace skyblip::formation
