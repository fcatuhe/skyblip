#include "products/skyblip_go/services/alarm.h"

#include <algorithm>

namespace skyblip::go {

bool AlarmService::silenced(traffic::Target& target, formation::State state) {
    if (state == formation::State::None) return false;
    tracker_.withdraw(target.observation.address_mapping_table, target.observation.address);
    target.alarm_level = traffic::Level::None;
    target.alarm_dismissed = false;
    return true;
}

formation::State AlarmService::watch_formation(traffic::Target& target, uint32_t now_ms) {
    const formation::Report report =
        formation_.observe(context_.state.own, target.observation, target.liftoff, now_ms);
    const bool was_in_formation = target.in_formation;
    target.in_formation = report.state == formation::State::Together;
    if (target.in_formation != was_in_formation) dirty_ = true;
    return report.state;
}

void AlarmService::tick(uint32_t now_ms) {
    accrue_annunciator(now_ms);
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

    if (worst != context_.state.alarm_level || live != context_.state.alarm_undismissed) {
        context_.state.alarm_level = worst;
        context_.state.alarm_undismissed = live;
        dirty_ = true;
    }

    annunciation::Situation situation{};
    // Not the raw worst: what is being announced, which the tracker already
    // holds through a target bouncing across a ring boundary, and which falls
    // to nothing when the target that caused it stops being heard.
    situation.level = tracker_.announced_level(now_ms);
    situation.announced = announced;
    situation.first_fix = context_.state.own.fix_acquired;
    situation.enabled = settings_.alarm_enabled && settings_.alarm_volume != AlarmVolume::Off;
    situation.running = running_;
    drive(situation, now_ms);
    drive_lamp(now_ms, running_);
}

void AlarmService::accrue_annunciator(uint32_t now_ms) {
    sounding_.observe(annunciation_.sounding(), now_ms);
    vibrating_.observe(annunciation_.vibrating(), now_ms);
    context_.state.duty.annunciator_ms = sounding_.on_ms() + vibrating_.on_ms();
}

// INFO: fc 20sep26 one record per reception: between two of them nothing new is known about it
void AlarmService::record_traffic(int slot, const traffic::Target& target,
                                  const traffic::AlarmTracker::Decision& decision,
                                  uint32_t now_ms) {
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
    context_.diagnostics.record(value, context_.instant(now_ms));
}

void AlarmService::park(uint32_t now_ms) {
    accrue_annunciator(now_ms);
    running_ = false;
    annunciation::Situation situation{};
    situation.running = false;
    drive(situation, now_ms);
    // Dark first, through the same table that lit it, and only then let go of the
    // pins. The order matters on silicon: these LEDs are active-low, so dark is a
    // pin driven high, and releasing before darkening would leave the last colour
    // lit on a floating line for as long as the rail lasts.
    drive_lamp(now_ms, /*running=*/false);
    context_.roles.indicator.park();
}

// Everything the table reads is already published on bus::State by the service
// that owns it - the cell and its level by PowerService, the fix by
// OwnshipService, the worst standing level by this one, a few lines above. Nothing
// is derived a second time here, which is what keeps the lamp saying LOW at
// exactly the voltage the panel and the tablet do.
void AlarmService::drive_lamp(uint32_t now_ms, bool running) {
    indication::Situation situation{};
    situation.running = running;
    situation.alarm_level = context_.state.alarm_undismissed;
    situation.power_level = context_.state.power.level;
    situation.fix_valid = context_.state.own.fix_valid;

    const indication::Command command = indication_.update(situation, now_ms);
    if (command.changed) context_.roles.indicator.show(command.lamp);
}

void AlarmService::drive(const annunciation::Situation& situation, uint32_t now_ms) {
    const annunciation::Command command = annunciation_.update(situation, now_ms);
    if (command.tone_changed) drive_buzzer(command);
    if (command.vibration_changed) drive_motor(command.vibrating);
}

void AlarmService::drive_buzzer(const annunciation::Command& command) {
    if (command.tone_on) {
        const ports::Loudness loudness = settings_.alarm_volume == AlarmVolume::Soft
                                             ? ports::Loudness::Soft
                                             : ports::Loudness::Loud;
        context_.roles.annunciator.tone(command.tone_hz, loudness);
    } else {
        context_.roles.annunciator.silence();
    }
}

void AlarmService::drive_motor(bool vibrating) {
    if (vibrating) {
        context_.roles.annunciator.vibrate();
    } else {
        context_.roles.annunciator.still();
    }
}

}  // namespace skyblip::go
