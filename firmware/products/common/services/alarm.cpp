#include "products/common/services/alarm.h"

namespace skyblip::common {

void AlarmService::tick(uint32_t now_ms) {
    accrue_annunciator(now_ms);
    const Alarm alarm = alarm_in_force();
    const Announcement announcement = grader_.grade(alarm, now_ms);
    annunciation::Situation situation{};
    situation.level = announcement.level;
    situation.announced = announcement.announced;
    situation.first_fix = context_.state.own.fix_acquired;
    const bool bravo_lost = comms::lost(context_.state.bravo);
    situation.peer_lost = (bravo_lost && !bravo_lost_) || lost_go();
    bravo_lost_ = bravo_lost;
    situation.enabled = audible(alarm);
    situation.running = running_;
    if (sign_on_pending_) {
        sign_on_pending_ = false;
        sign_.begin(indication::kSignOn, now_ms);
    }
    const annunciation::Command command = annunciation_.update(situation, now_ms);
    if (annunciation_.voice() == annunciation::Voice::Traffic) sign_.cancel();
    const indication::Signal signal = sign_.update(now_ms);
    if (command.tone_changed) drive_buzzer(command);
    drive_motor(sign_.playing() ? signal.vibrating : command.vibrating);
    drive_lamp(now_ms, running_, signal);
}

// INFO: fc 17nov26 a plain Go plays no lost-Bravo tone, so a Bravo in flight says it lost its Go
bool AlarmService::lost_go() {
    const bool following = context_.state.followed_call.has_value();
    const bool airborne = context_.state.flight.confirmed_state == flight::FlightState::Airborne;
    const bool lost = following_go_ && !following && settings_.paired && airborne;
    following_go_ = following;
    return lost;
}

void AlarmService::sign_off(uint32_t now_ms) {
    accrue_annunciator(now_ms);
    running_ = false;
    annunciation::Situation situation{};
    situation.running = false;
    const annunciation::Command command = annunciation_.update(situation, now_ms);
    if (command.tone_changed) drive_buzzer(command);
    drive_motor(false);
    // INFO: fc 13nov26 a Bravo refused to fly lit the lamp around this service, so dark is told,
    // not compared
    shown_ = indication::Colour::None;
    context_.roles.indicator.show(indication::Colour::None);
    sign_off_pending_ = true;
}

void AlarmService::advance_sign(uint32_t now_ms) {
    accrue_annunciator(now_ms);
    if (sign_off_pending_) {
        sign_off_pending_ = false;
        sign_.begin(indication::kSignOff, now_ms);
    }
    const indication::Signal signal = sign_.update(now_ms);
    drive_motor(signal.vibrating);
    show(signal.lamp);
}

void AlarmService::accrue_annunciator(uint32_t now_ms) {
    sounding_.observe(annunciation_.sounding(), now_ms);
    vibrating_.observe(motor_on_, now_ms);
    context_.state.duty.annunciator_ms = sounding_.on_ms() + vibrating_.on_ms();
}

void AlarmService::park(uint32_t now_ms) {
    accrue_annunciator(now_ms);
    running_ = false;
    sign_.cancel();
    sign_off_pending_ = false;
    annunciation::Situation situation{};
    situation.running = false;
    const annunciation::Command command = annunciation_.update(situation, now_ms);
    if (command.tone_changed) drive_buzzer(command);
    drive_motor(false);
    // Dark first, through the same table that lit it, and only then let go of the
    // pins. The order matters on silicon: these LEDs are active-low, so dark is a
    // pin driven high, and releasing before darkening would leave the last colour
    // lit on a floating line for as long as the rail lasts.
    drive_lamp(now_ms, /*running=*/false, indication::Signal{});
    context_.roles.indicator.park();
}

// Everything the table reads is already published on bus::State by the service
// that owns it - the cell and its level by PowerService, the fix by
// OwnshipService, the worst standing level by this one's AlarmGrader. Nothing
// is derived a second time here, which is what keeps the lamp saying LOW at
// exactly the voltage the glass and the tablet do.
void AlarmService::drive_lamp(uint32_t now_ms, bool running, indication::Signal signal) {
    indication::Situation situation{};
    situation.running = running;
    situation.alarm_level = context_.state.alarm_undismissed;
    situation.power_level = context_.state.power.level;
    situation.fix_valid = context_.state.own.fix_valid;
    situation.prompting = lamp_shows_link_ && context_.state.link.prompt_standing;
    situation.pairing = lamp_shows_link_ && context_.state.link.pairing_standing;
    situation.linked = lamp_shows_link_ && context_.state.link.phone_connected;
    situation.capturing = context_.state.capture.armed;

    const indication::Command command = indication_.update(situation, now_ms, context_.state.clock);
    show(sign_.playing() ? signal.lamp : command.lamp);
}

void AlarmService::show(indication::Colour colour) {
    if (colour == shown_) return;
    shown_ = colour;
    context_.roles.indicator.show(colour);
}

void AlarmService::drive_buzzer(const annunciation::Command& command) {
    if (command.tone_on) {
        const ports::Loudness loudness =
            alarm_in_force() == Alarm::Soft ? ports::Loudness::Soft : ports::Loudness::Loud;
        context_.roles.annunciator.tone(command.tone_hz, loudness);
    } else {
        context_.roles.annunciator.silence();
    }
}

void AlarmService::drive_motor(bool vibrating) {
    if (vibrating == motor_on_) return;
    motor_on_ = vibrating;
    if (vibrating) {
        context_.roles.annunciator.vibrate();
    } else {
        context_.roles.annunciator.still();
    }
}

}  // namespace skyblip::common
