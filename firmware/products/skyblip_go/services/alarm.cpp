#include "products/skyblip_go/services/alarm.h"

#include <algorithm>

namespace skyblip::go {

bool AlarmService::silenced(traffic::Target& target, formation::State state,
                            const traffic::AlarmAssessment& a, uint32_t now_ms) {
    if (state == formation::State::None) return false;
    if (a.closing_mps >= formation::kClosingMps) {
        formation_.release(target.obs.addr_table, target.obs.addr, now_ms);
        target.in_formation = false;
        return false;
    }
    tracker_.withdraw(target.obs.addr_table, target.obs.addr);
    target.alarm_level = traffic::Level::None;
    target.alarm_dismissed = false;
    return true;
}

formation::State AlarmService::watch_formation(traffic::Target& target, uint32_t now_ms) {
    const formation::Report r = formation_.observe(context_.state.own, target.obs, now_ms);
    const bool was = target.in_formation;
    target.in_formation = r.state == formation::State::Together;
    if (target.in_formation != was) dirty_ = true;
    return r.state;
}

void AlarmService::tick(uint32_t now_ms) {
    accrue_annunciator(now_ms);
    traffic::Level worst = traffic::Level::None;
    traffic::Level live = traffic::Level::None;
    bool announced = false;
    if (context_.state.own.fix_valid) {
        for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
            traffic::Target* t = context_.state.traffic.at(i);
            if (!t || !t->used) continue;
            const formation::State f = watch_formation(*t, now_ms);
            const traffic::AlarmTracker::Decision d =
                tracker_.update(context_.state.own, t->obs, now_ms);
            t->alarm_level = d.assessment.level;
            t->alarm_dismissed = d.dismissed;
            const bool quiet = silenced(*t, f, d.assessment, now_ms);
            record_traffic(i, *t, d, now_ms);
            if (quiet) continue;
            worst = std::max(d.assessment.level, worst);
            if (!d.dismissed) live = std::max(d.assessment.level, live);
            announced = announced || d.notify;
        }
    }
    tracker_.forget_stale(now_ms);
    formation_.forget_stale(now_ms);
    context_.state.formation.members = formation_.members();

    if (worst != context_.state.alarm_level || live != context_.state.alarm_live) {
        context_.state.alarm_level = worst;
        context_.state.alarm_live = live;
        dirty_ = true;
    }

    annunciation::Situation situation{};
    // Not the raw worst: what is being announced, which the tracker already
    // holds through a contact bouncing across a ring boundary, and which falls
    // to nothing when the target that caused it stops being heard.
    situation.level = tracker_.announced_level(now_ms);
    situation.announced = announced;
    situation.first_fix = context_.state.own.fix_acquired;
    situation.enabled = settings_.alarm_enabled;
    situation.running = running_;
    drive(situation, now_ms);
    drive_lamp(now_ms, running_);

    if (!situation.enabled || !running_) return;
    if (announced) pulse_haptic();
}

void AlarmService::accrue_annunciator(uint32_t now_ms) {
    sounding_.observe(policy_.sounding(), now_ms);
    context_.state.duty.annunciator_ms = sounding_.ms() + haptic_ms_;
}

void AlarmService::pulse_haptic() {
    context_.roles.annunciator.vibrate(kHapticFeltThroughAHarnessMs);
    haptic_ms_ += kHapticFeltThroughAHarnessMs;
}

// INFO: fc 20sep26 one record per reception: between two of them nothing new is known about it
void AlarmService::record_traffic(int slot, const traffic::Target& target,
                                  const traffic::AlarmTracker::Decision& decision,
                                  uint32_t now_ms) {
    if (target.obs.at_ms == recorded_obs_ms_[slot]) return;
    recorded_obs_ms_[slot] = target.obs.at_ms;
    if (!context_.diag.armed()) return;

    const traffic::AlarmAssessment& assessment = decision.assessment;
    diag::Traffic value{};
    value.addr = target.obs.addr;
    value.dist_m = diag::clamp_u16(
        assessment.rel_dist_m < 0 ? 0 : static_cast<uint32_t>(assessment.rel_dist_m));
    value.bearing_deg = assessment.rel_bearing_deg;
    value.rel_alt_m = diag::clamp_i16(assessment.rel_alt_m);
    value.closing_mps = diag::clamp_i16(assessment.closing_mps);
    value.alarm = target.alarm_level;
    value.source = target.obs.source;
    value.rssi_dbm = target.obs.rssi_dbm;
    value.tracked = static_cast<uint8_t>(context_.state.traffic.count());
    value.assessed = assessment.valid;
    value.dismissed = decision.dismissed;
    value.in_formation = target.in_formation;
    value.position_valid = target.obs.position_valid;
    context_.diag.record(value, context_.instant(now_ms));
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
    situation.alarm_level = context_.state.alarm_live;
    situation.power_level = context_.state.power.level;
    situation.cell_caution = context_.state.power.caution;
    situation.fix_valid = context_.state.own.fix_valid;

    const indication::Command command = lamp_.update(situation, now_ms);
    if (command.changed) context_.roles.indicator.show(command.lamp);
}

void AlarmService::drive(const annunciation::Situation& situation, uint32_t now_ms) {
    const annunciation::Command command = policy_.update(situation, now_ms);
    if (!command.changed) return;
    if (!command.tone_on) {
        context_.roles.annunciator.silence();
        return;
    }
    const uint8_t volume = settings_.alarm_volume;
    if (command.tone_hz != 0)
        context_.roles.annunciator.tone(command.tone_hz, volume);
    else
        context_.roles.annunciator.alarm(command.tone_level, volume);
}

}  // namespace skyblip::go
