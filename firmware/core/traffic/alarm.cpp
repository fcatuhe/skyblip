#include "core/traffic/alarm.h"

#include <algorithm>

#include "core/flight/extrapolate.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/nmea_out.h"
#include "core/traffic/lease.h"
#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::traffic {

namespace {

constexpr int32_t kTrigOne = 16384;

// Millimetres a second, north and east, scaled by the cordic one.
void velocity_ned(int32_t speed_mm_s, int16_t angle, int64_t& north, int64_t& east) {
    north = static_cast<int64_t>(speed_mm_s) * icos(angle);
    east = static_cast<int64_t>(speed_mm_s) * isin(angle);
}

int32_t closing_from_vectors(const model::OwnState& own, const model::AircraftObs& target,
                             int32_t n_m, int32_t e_m, int32_t dist_m) {
    if (dist_m <= 0) return kUnknownTargetSpeedMps;

    int64_t own_n = 0, own_e = 0;
    velocity_ned(own.speed_mm_s, to_angle16(CentiDegrees(own.track_cdeg)), own_n, own_e);
    int64_t target_n = 0, target_e = 0;
    if (target.speed_valid)
        velocity_ned(to_mm_s(QuarterMetresPerSec(target.speed_q)).v,
                     to_angle16(Cordic9(target.track_c9)), target_n, target_e);

    const int64_t along = (target_n - own_n) * n_m + (target_e - own_e) * e_m;
    const int64_t scale = static_cast<int64_t>(dist_m) * kTrigOne * kMillimetresPerMetre;
    int32_t closing = static_cast<int32_t>(div_round<int64_t>(-along, scale));
    if (!target.speed_valid) closing += kUnknownTargetSpeedMps;
    return closing;
}

int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

Level level_for(const AlarmAssessment& a) {
    const bool in_window = iabs32(a.rel_alt_m) <= kAdvisoryAltM && a.rel_dist_m <= kAdvisoryDistM;
    return in_window ? Level::Advisory : Level::None;
}

}  // namespace

// INFO: fc 13sep26 two positions from different instants are not a separation, so both are carried
// to now
AlarmAssessment assess(const model::OwnState& own_fix, const model::AircraftObs& reported,
                       uint32_t now_ms) {
    const model::OwnState own = flight::carried_to(own_fix, now_ms);
    const model::AircraftObs target = flight::carried_to(reported, now_ms);
    AlarmAssessment a{};
    int32_t n_m, e_m, u_m;
    if (!protocol::relative_ned(own, target, n_m, e_m, u_m)) return a;
    a.valid = true;
    a.rel_alt_m = u_m;
    a.rel_dist_m = static_cast<int32_t>(idistance(n_m, e_m));
    a.closing_mps = closing_from_vectors(own, target, n_m, e_m, a.rel_dist_m);

    const int16_t brg = iatan2(e_m, n_m);
    const int own_deg = to_degrees(CentiDegrees(own.track_cdeg)).v;
    const int brg_deg = static_cast<int>(
        div_round<int32_t>(static_cast<int32_t>(static_cast<uint16_t>(brg)) * 360, 65536) % 360);
    a.rel_bearing_deg = static_cast<uint16_t>(((brg_deg - own_deg) % 360 + 360) % 360);

    a.level = flight::on_ground(reported.flight_state) ? Level::None : level_for(a);
    return a;
}

AlarmTracker::Decision AlarmTracker::update(const model::OwnState& own,
                                            const model::AircraftObs& target, uint32_t now_ms) {
    Decision d{};
    d.assessment = assess(own, target, now_ms);
    if (!d.assessment.valid) return d;

    Slot* slot = slot_for(target, now_ms);
    if (slot == nullptr) return d;

    const uint32_t key = target.received.at_s * 1000u + target.received.into_ms;
    if (key != slot->obs_key) {
        slot->obs_key = key;
        slot->seen_ms = now_ms;
    }
    slot->forget_ms = forget_ms(target);

    if (now_ms - target.at_ms <= kAlertMaxAgeMs)
        d.notify = notify_for(*slot, d.assessment.level, now_ms);
    if (d.notify) slot->dismissed = false;
    if (slot->dismissed) d.notify = false;
    d.dismissed = slot->dismissed;
    return d;
}

bool AlarmTracker::notify_for(Slot& slot, Level level, uint32_t now_ms) {
    const bool speak = level > slot.notified_level;

    if (level < slot.notified_level) {
        if (!slot.falling) {
            slot.falling = true;
            slot.falling_since_ms = now_ms;
        } else if (now_ms - slot.falling_since_ms >= kRenotifyFloorMs) {
            slot.notified_level = level;
            slot.falling = false;
        }
    } else {
        slot.falling = false;
    }

    if (speak) slot.notified_level = level;
    return speak;
}

AlarmTracker::Slot* AlarmTracker::slot_for(const model::AircraftObs& target, uint32_t now_ms) {
    Slot* free_slot = nullptr;
    Slot* oldest = nullptr;
    for (Slot& s : slots_) {
        if (s.used && s.addr == target.addr && s.addr_table == target.addr_table) return &s;
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
    slot->addr = target.addr;
    slot->addr_table = target.addr_table;
    slot->seen_ms = now_ms;
    return slot;
}

void AlarmTracker::withdraw(uint8_t addr_table, uint32_t addr) {
    for (Slot& s : slots_) {
        if (!s.used || s.addr != addr || s.addr_table != addr_table) continue;
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
