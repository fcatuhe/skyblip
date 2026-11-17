#include "products/common/alarm_grader.h"

#include <algorithm>

#include "core/diagnostics/payload.h"
#include "core/util/clamp.h"

namespace skyblip::common {

Announcement AlarmGrader::grade(Alarm alarm, uint32_t now_ms) {
    traffic::Level worst = traffic::Level::None;
    traffic::Level live = traffic::Level::None;
    bool announced = false;
    formation_.observe_own(context_.state.own, now_ms);
    if (context_.state.own.fix_valid) {
        for (int i = 0; i < traffic::TrafficTable::kMaxTargets; i++) {
            traffic::Target* target = context_.state.traffic.at(i);
            if (!target || !target->used) continue;
            const formation::State formation_state = watch_formation(*target, now_ms);
            const traffic::AlarmTracker::Decision decision =
                tracker_.update(context_.state.own, target->observation, now_ms);
            target->alarm_level = decision.assessment.level;
            target->alarm_dismissed = decision.dismissed;
            const bool quiet = silenced(*target, formation_state);
            record_traffic(i, *target, decision, now_ms);
            if (quiet) continue;
            worst = std::max(decision.assessment.level, worst);
            if (!decision.dismissed) live = std::max(decision.assessment.level, live);
            announced = announced || decision.notify;
        }
    }
    tracker_.forget_stale(now_ms);
    formation_.forget_stale(now_ms);
    context_.state.formation.members = formation_.members();

    // INFO: fc 15nov26 off interrupts nothing, and each target keeps its level for radar and NMEA
    if (!interrupts(alarm)) live = traffic::Level::None;
    if (announced) call_.announcement++;
    // INFO: fc 17nov26 the tracker's level, held through a ring bounce, not the raw worst
    call_.announcing = tracker_.announced_level(now_ms);
    call_.undismissed = live;
    context_.state.alarm_call = call_;

    Announcement announcement{call_.announcing, announced};
    // INFO: fc 17nov26 a paired Bravo plays its Go's calls, and grades its own only for NMEA
    if (context_.state.followed_call) {
        const annunciation::Follower::Heard heard = follower_.follow(*context_.state.followed_call);
        announcement = Announcement{heard.level, heard.announced};
        live = interrupts(alarm) ? heard.undismissed : traffic::Level::None;
    } else {
        follower_.lose();
    }
    context_.state.alarm_level = worst;
    context_.state.alarm_undismissed = live;
    return announcement;
}

formation::State AlarmGrader::watch_formation(traffic::Target& target, uint32_t now_ms) {
    const formation::Report report =
        formation_.observe(context_.state.own, target.observation, target.liftoff, now_ms);
    target.in_formation = report.state == formation::State::Together;
    return report.state;
}

bool AlarmGrader::silenced(traffic::Target& target, formation::State state) {
    if (state == formation::State::None) return false;
    tracker_.withdraw(target.observation.address_mapping_table, target.observation.address);
    target.alarm_level = traffic::Level::None;
    target.alarm_dismissed = false;
    return true;
}

// INFO: fc 20sep26 one record per reception: between two of them nothing new is known about it
void AlarmGrader::record_traffic(int slot, const traffic::Target& target,
                                 const traffic::AlarmTracker::Decision& decision, uint32_t now_ms) {
    if (target.observation.at_ms == recorded_observation_ms_[slot]) return;
    recorded_observation_ms_[slot] = target.observation.at_ms;
    if (!context_.diagnostics.armed()) return;

    const traffic::AlarmAssessment& assessment = decision.assessment;
    diagnostics::Traffic value{};
    value.address = target.observation.address;
    value.distance_m = clamp_uint16(assessment.relative_distance_m < 0
                                        ? 0
                                        : static_cast<uint32_t>(assessment.relative_distance_m));
    value.bearing_deg = assessment.relative_bearing_deg;
    value.relative_altitude_m = clamp_int16(assessment.relative_altitude_m);
    value.closing_mps = clamp_int16(assessment.closing_mps);
    value.alarm = target.alarm_level;
    value.source = target.observation.source;
    value.rssi_dbm = target.observation.rssi_dbm;
    value.tracked = static_cast<uint8_t>(context_.state.traffic.count());
    value.assessed = assessment.valid;
    value.dismissed = decision.dismissed;
    value.in_formation = target.in_formation;
    value.position_valid = target.observation.position_valid;
    value.relative_altitude_valid = assessment.relative_altitude_valid;
    context_.diagnostics.record(value, context_.instant(now_ms));
}

}  // namespace skyblip::common
