#include "core/traffic/alarm.h"

#include <algorithm>
#include <cstdlib>

#include "core/flight/extrapolate.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/traffic/lease.h"
#include "core/traffic/range.h"
#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::traffic {

namespace {

// Millimetres a second, north and east, scaled by the cordic one.
void velocity_ned(int32_t speed_mm_s, int16_t angle, int64_t& north, int64_t& east) {
    north = static_cast<int64_t>(speed_mm_s) * icos(angle);
    east = static_cast<int64_t>(speed_mm_s) * isin(angle);
}

Level level_for(const AlarmAssessment& assessment) {
    const bool in_window =
        std::abs(assessment.rel_alt_m) <= kAdvisoryAltM && assessment.rel_dist_m <= kAdvisoryDistM;
    return in_window ? Level::Advisory : Level::None;
}

}  // namespace

int32_t closing_mm_s(const model::Ownship& own, const model::AircraftObservation& target,
                     int32_t north_m, int32_t east_m, int32_t dist_m) {
    constexpr int32_t unknown_mm_s = kUnknownTargetSpeedMps * kMillimetresPerMetre;
    if (dist_m <= 0) return unknown_mm_s;

    int64_t own_n = 0, own_e = 0;
    velocity_ned(own.speed_mm_s, to_angle16(CentiDegrees(own.track_cdeg)), own_n, own_e);
    int64_t target_n = 0, target_e = 0;
    if (target.speed_valid)
        velocity_ned(to_mm_s(QuarterMetresPerSecond(target.speed_q)).v,
                     to_angle16(Cordic9(target.track_c9)), target_n, target_e);

    const int64_t along = (target_n - own_n) * north_m + (target_e - own_e) * east_m;
    const int64_t scale = static_cast<int64_t>(dist_m) * kTrigOne;
    int32_t closing = static_cast<int32_t>(div_round<int64_t>(-along, scale));
    if (!target.speed_valid) closing += unknown_mm_s;
    return closing;
}

// INFO: fc 13sep26 two positions from different instants are not a separation, so both are carried
// to now
AlarmAssessment assess(const model::Ownship& own_fix, const model::AircraftObservation& reported,
                       uint32_t now_ms) {
    const model::Ownship own = flight::carried_to(own_fix, now_ms);
    const model::AircraftObservation target = flight::carried_to(reported, now_ms);
    AlarmAssessment assessment{};
    int32_t north_m, east_m, up_m;
    if (!relative_ned(own, target, north_m, east_m, up_m)) return assessment;
    assessment.valid = true;
    assessment.rel_alt_m = up_m;
    assessment.rel_dist_m = static_cast<int32_t>(idistance(north_m, east_m));
    assessment.closing_mps = div_round(
        closing_mm_s(own, target, north_m, east_m, assessment.rel_dist_m), kMillimetresPerMetre);

    const int16_t bearing = iatan2(east_m, north_m);
    const int own_deg = to_degrees(CentiDegrees(own.track_cdeg)).v;
    const int bearing_deg = static_cast<int>(
        div_round<int32_t>(static_cast<int32_t>(static_cast<uint16_t>(bearing)) * 360, 65536) %
        360);
    assessment.rel_bearing_deg = static_cast<uint16_t>(((bearing_deg - own_deg) % 360 + 360) % 360);

    const bool grounded =
        flight::on_ground(reported.flight_state) || flight::on_ground(own.flight_state);
    assessment.level = grounded ? Level::None : level_for(assessment);
    return assessment;
}

AlarmTracker::Decision AlarmTracker::update(const model::Ownship& own,
                                            const model::AircraftObservation& target,
                                            uint32_t now_ms) {
    Decision decision{};
    decision.assessment = assess(own, target, now_ms);
    if (!decision.assessment.valid) return decision;

    Slot* slot = slot_for(target, now_ms);
    if (slot == nullptr) return decision;

    const uint32_t key = target.received.at_s * 1000u + target.received.into_ms;
    if (key != slot->reception_key) {
        slot->reception_key = key;
        slot->seen_ms = now_ms;
    }
    slot->forget_ms = forget_ms(target);

    const bool grounded =
        flight::on_ground(target.flight_state) || flight::on_ground(own.flight_state);
    const uint32_t floor_ms = grounded ? kGroundRenotifyFloorMs : kRenotifyFloorMs;
    if (now_ms - target.at_ms <= kAlertMaxAgeMs)
        decision.notify = notify_for(*slot, decision.assessment.level, floor_ms, now_ms);
    if (decision.notify) slot->dismissed = false;
    if (slot->dismissed) decision.notify = false;
    decision.dismissed = slot->dismissed;
    return decision;
}

bool AlarmTracker::notify_for(Slot& slot, Level level, uint32_t floor_ms, uint32_t now_ms) {
    const bool speak = level > slot.notified_level;

    if (level < slot.notified_level) {
        if (!slot.falling) {
            slot.falling = true;
            slot.falling_since_ms = now_ms;
        } else if (now_ms - slot.falling_since_ms >= floor_ms) {
            slot.notified_level = level;
            slot.falling = false;
        }
    } else {
        slot.falling = false;
    }

    if (speak) slot.notified_level = level;
    return speak;
}

AlarmTracker::Slot* AlarmTracker::slot_for(const model::AircraftObservation& target,
                                           uint32_t now_ms) {
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

void AlarmTracker::withdraw(uint8_t address_mapping_table, uint32_t address) {
    for (Slot& s : slots_) {
        if (!s.used || s.address != address || s.address_mapping_table != address_mapping_table)
            continue;
        s.notified_level = Level::None;
        s.falling = false;
        s.dismissed = false;
    }
}

void AlarmTracker::dismiss() {
    for (Slot& s : slots_)
        if (s.used && s.notified_level != Level::None) s.dismissed = true;
}

bool AlarmTracker::dismissed() const {
    bool any = false;
    for (const Slot& s : slots_) {
        if (!s.used || s.notified_level == Level::None) continue;
        if (!s.dismissed) return false;
        any = true;
    }
    return any;
}

void AlarmTracker::forget_stale(uint32_t now_ms) {
    for (Slot& s : slots_) {
        if (s.used && now_ms - s.seen_ms > s.forget_ms) s = Slot{};
    }
}

Level AlarmTracker::announced_level(uint32_t now_ms) const {
    Level level = Level::None;
    for (const Slot& s : slots_) {
        if (!s.used || s.dismissed || now_ms - s.seen_ms > kAlertMaxAgeMs) continue;
        level = std::max(s.notified_level, level);
    }
    return level;
}

}  // namespace skyblip::traffic
