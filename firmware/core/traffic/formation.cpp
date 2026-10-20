#include "core/traffic/formation.h"

#include <cstdlib>

#include "core/flight/extrapolate.h"
#include "core/flight/state.h"
#include "core/traffic/alarm.h"
#include "core/traffic/lease.h"
#include "core/traffic/range.h"
#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::formation {

namespace {

void heading_up(int32_t north_m, int32_t east_m, int32_t track_cdeg, int32_t& ahead_m,
                int32_t& right_m) {
    const int16_t track = to_angle16(CentiDegrees(track_cdeg));
    const int64_t cosine_of_track = cosine(track), sine_of_track = sine(track);
    ahead_m = static_cast<int32_t>(
        divide_rounded<int64_t>(north_m * cosine_of_track + east_m * sine_of_track, kTrigOne));
    right_m = static_cast<int32_t>(
        divide_rounded<int64_t>(east_m * cosine_of_track - north_m * sine_of_track, kTrigOne));
}

uint32_t reception_key(const model::AircraftObservation& observation) {
    return observation.received.at_s * kMillisecondsPerSecond + observation.received.into_ms;
}

bool recent(const flight::Liftoff& liftoff, uint32_t now_ms) {
    return liftoff.valid && now_ms - liftoff.at_ms < kLiftoffWindowMs;
}

}  // namespace

void Tracker::observe_own(const model::Ownship& own, uint32_t now_ms) {
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
    slot.anchor_ahead_m = station.ahead_m;
    slot.anchor_right_m = station.right_m;
    slot.anchor_up_m = station.up_m;
    slot.on_station_since_ms = now_ms;
    slot.drift_fixes = 0;
    slot.closing_fixes = 0;
    slot.anchored = true;
}

Report Tracker::observe(const model::Ownship& own_fix, const model::AircraftObservation& reported,
                        const flight::Liftoff& target_liftoff, uint32_t now_ms) {
    Report out{};
    if (flight::on_ground(own_fix.flight_state) || flight::on_ground(reported.flight_state)) {
        drop(reported.address_mapping_table, reported.address);
        return out;
    }
    const model::Ownship own = flight::carried_to(own_fix, now_ms);
    const model::AircraftObservation target = flight::carried_to(reported, now_ms);
    int32_t north_m, east_m, up_m;
    if (!flight::relative_north_east_up(own, target, north_m, east_m, up_m)) return out;

    heading_up(north_m, east_m, own.track_cdeg, out.ahead_m, out.right_m);
    out.up_m = up_m;
    out.valid = true;

    Slot* slot = slot_for(target, now_ms);
    if (slot == nullptr) return out;
    slot->seen_ms = now_ms;

    const int32_t range_m = static_cast<int32_t>(hypotenuse(north_m, east_m));
    if (range_m > kRangeM || std::abs(up_m) > kVerticalM) {
        *slot = Slot{};
        return out;
    }

    const uint32_t key = reception_key(reported);
    if (slot->anchored && key == slot->reception_key) {
        out.state = slot->state;
        return out;
    }
    slot->reception_key = key;

    if (!slot->anchored) {
        anchor(*slot, out, now_ms);
        if (lifting(target_liftoff, now_ms)) slot->state = State::Joining;
        out.state = slot->state;
        return out;
    }

    if (closure_confirmed(*slot, traffic::closing_mm_s(own, target, north_m, east_m, range_m))) {
        anchor(*slot, out, now_ms);
        slot->state = State::None;
        return out;
    }

    const int32_t drift_m = static_cast<int32_t>(
        hypotenuse(out.ahead_m - slot->anchor_ahead_m, out.right_m - slot->anchor_right_m));
    const bool station_kept =
        drift_m <= kDriftM && std::abs(out.up_m - slot->anchor_up_m) <= kVerticalDriftM;

    if (station_kept) {
        slot->drift_fixes = 0;
        if (slot->state != State::Together && now_ms - slot->on_station_since_ms >= kTogetherHoldMs)
            slot->state = State::Together;
    } else {
        off_station(*slot, out, now_ms);
    }
    out.state = slot->state;
    return out;
}

bool Tracker::lifting(const flight::Liftoff& target_liftoff, uint32_t now_ms) const {
    return recent(own_liftoff_, now_ms) || recent(target_liftoff, now_ms);
}

bool Tracker::closure_confirmed(Slot& slot, int32_t closing_mm_s) {
    if (slot.state == State::None || closing_mm_s < kClosingMps * kMillimetresPerMetre) {
        slot.closing_fixes = 0;
        return false;
    }
    return ++slot.closing_fixes >= kClosingFixes;
}

void Tracker::off_station(Slot& slot, const Report& fix, uint32_t now_ms) {
    slot.on_station_since_ms = now_ms;
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
    int count = 0;
    for (const Slot& slot : slots_)
        if (slot.used && slot.state == State::Together) count++;
    return count;
}

void Tracker::forget_stale(uint32_t now_ms) {
    constexpr uint32_t lease_ms = traffic::kAirborneTargetForgetS * kMillisecondsPerSecond;
    for (Slot& slot : slots_)
        if (slot.used && now_ms - slot.seen_ms > lease_ms) slot = Slot{};
}

void Tracker::drop(uint8_t address_mapping_table, uint32_t address) {
    Slot* slot = find(address_mapping_table, address);
    if (slot != nullptr) *slot = Slot{};
}

Tracker::Slot* Tracker::slot_for(const model::AircraftObservation& target, uint32_t now_ms) {
    Slot* free_slot = nullptr;
    Slot* oldest = nullptr;
    for (Slot& candidate : slots_) {
        if (candidate.used && candidate.address == target.address &&
            candidate.address_mapping_table == target.address_mapping_table)
            return &candidate;
        if (!candidate.used) {
            if (free_slot == nullptr) free_slot = &candidate;
            continue;
        }
        if (candidate.state == State::Together) continue;
        if (oldest == nullptr || now_ms - candidate.seen_ms > now_ms - oldest->seen_ms)
            oldest = &candidate;
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
    for (const Slot& slot : slots_)
        if (slot.used && slot.address == address &&
            slot.address_mapping_table == address_mapping_table)
            return &slot;
    return nullptr;
}

Tracker::Slot* Tracker::find(uint8_t address_mapping_table, uint32_t address) {
    for (Slot& slot : slots_)
        if (slot.used && slot.address == address &&
            slot.address_mapping_table == address_mapping_table)
            return &slot;
    return nullptr;
}

}  // namespace skyblip::formation
