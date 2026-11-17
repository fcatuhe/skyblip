#ifndef SKYBLIP_PRODUCTS_COMMON_SERVICES_ALARM_H
#define SKYBLIP_PRODUCTS_COMMON_SERVICES_ALARM_H

#include <optional>

#include "core/annunciation/annunciator.h"
#include "core/indication/lamp.h"
#include "core/indication/sign.h"
#include "core/power/duty.h"
#include "ports/indicator.h"
#include "products/common/alarm.h"
#include "products/common/alarm_grader.h"
#include "products/common/features.h"
#include "runtime/service.h"
#include "runtime/tasks.h"

namespace skyblip::common {

struct AlarmSettings {
    explicit AlarmSettings(const Alarm& alarm_of) : AlarmSettings(alarm_of, alarm_of, kUnpaired) {}
    // INFO: fc 17nov26 Bravo plays its Go's alarm while paired, link down included
    AlarmSettings(const Alarm& alarm_of, const Alarm& go_alarm_of, const bool& paired_of)
        : alarm(alarm_of), go_alarm(go_alarm_of), paired(paired_of) {}
    explicit AlarmSettings(Alarm&&) = delete;
    explicit AlarmSettings(const Alarm&&) = delete;

    // INFO: fc 17nov26 a unit with no buzzer and no haptic shows a soft or loud alarm
    Alarm in_force(ports::Capabilities fitted) const {
        const Alarm set = paired ? go_alarm : alarm;
        return can_sound(fitted) || !audible(set) ? set : Alarm::Visual;
    }

    const Alarm& alarm;
    const Alarm& go_alarm;
    const bool& paired;

   private:
    static constexpr bool kUnpaired = false;
};

// The lamp lives here rather than in PowerService, whose state most of it reads,
// because the highest-priority row in the table is the alarm level and because a
// device going down has to be silenced, signed off and then darkened by the one
// owner of the lamp, the buzzer and the motor.
class AlarmService : public runtime::Service {
   public:
    AlarmService(runtime::Context& context, Feature declared, AlarmSettings settings)
        : runtime::Service(context), declared_(declared), settings_(settings) {}

    Status setup() override;
    void tick(uint32_t now_ms) override;

    // The device is going down: the loop stops running, so the buzzer is
    // released and the lamp is darkened and let go here rather than left latched
    // on a rail that is about to drop.
    void park(uint32_t now_ms);

    // INFO: fc 13nov26 asked before the first pass, and played from it, so the sign costs bring-up
    // nothing
    void sign_on() { sign_on_pending_ = true; }
    // INFO: fc 13nov26 the loop has stopped, so the product advances the sign and parks once
    // it ends
    void sign_off(uint32_t now_ms);
    // INFO: fc 13nov26 a sign off starts on the next pass, as the shutdown's own pass may block on
    // flash
    void advance_sign(uint32_t now_ms);
    bool signing() const { return sign_off_pending_ || sign_.playing(); }

    void dismiss() { grader_.dismiss(); }
    void dismiss_announcement(uint32_t announcement) { grader_.dismiss_announcement(announcement); }
    std::optional<uint32_t> dismissed_announcement() const {
        return grader_.dismissed_announcement();
    }
    Alarm alarm_in_force() const { return settings_.in_force(context_.roles.capabilities); }

    int formation_members() const { return grader_.formation_members(); }

    traffic::Level announcing_level() const { return annunciation_.announcing_level(); }
    bool sounding() const { return annunciation_.sounding(); }

    // What the table decided, and what the lamp is showing this instant. Both,
    // because they differ in the gaps of a wink and a test has to be able to say
    // which of the two it means.
    indication::Condition indicator_condition() const { return indication_.condition(); }
    indication::Colour lamp() const { return shown_; }

   private:
    bool lost_go();
    void drive_buzzer(const annunciation::Command& command);
    void drive_motor(bool vibrating);
    void drive_lamp(uint32_t now_ms, bool running, indication::Signal signal);
    void show(indication::Colour colour);
    void accrue_annunciator(uint32_t now_ms);

    // INFO: fc 16nov26 eight passes per alarm note or gap, so an alarm sounds as written
    static constexpr uint32_t kPassesPerShortestPhase = 8;
    static_assert(runtime::kServicePassCeilingMs * kPassesPerShortestPhase <=
                      annunciation::kShortestAlarmPhaseMs,
                  "the service loop is too coarse to resolve the shortest note");
    static_assert(annunciation::kAnnunciatorOnCeilingMs < runtime::kServiceSilenceCeilingMs,
                  "the annunciator's guard has to cut before the watchdog bites");

    // Three passes through the shortest flash in the table. An LED needs far less
    // time than a piezo to be seen, so the figure is smaller than the one above -
    // but a flash the loop cannot resolve is a flash nobody sees.
    static constexpr uint32_t kPassesPerShortestFlash = 3;
    static_assert(runtime::kServicePassCeilingMs * kPassesPerShortestFlash <=
                      indication::kShortestPhaseMs,
                  "the service loop is too coarse to resolve the shortest flash");
    static_assert(runtime::kServicePassCeilingMs * kPassesPerShortestFlash <=
                      indication::kSignShortestPhaseMs,
                  "the service loop is too coarse to resolve the shortest sign pulse");
    static_assert(indication::kSignOn.pulse_ms < annunciation::kAnnunciatorOnCeilingMs &&
                      indication::kSignOff.pulse_ms < annunciation::kAnnunciatorOnCeilingMs,
                  "the motor's guard would cut a sign pulse");
    static_assert(indication::kSignOn.pulses != annunciation::calls(annunciation::kAdvisoryTune) &&
                      indication::kSignOff.pulses !=
                          annunciation::calls(annunciation::kAdvisoryTune),
                  "a sign felt as many times as the advisory is felt as traffic");

    power::OnTime sounding_{};
    power::OnTime vibrating_{};
    AlarmGrader grader_{context_};
    bool bravo_lost_{false};
    bool following_go_{false};
    annunciation::Policy annunciation_{};
    indication::Policy indication_{};
    indication::SignPlayer sign_{};
    indication::Colour shown_{indication::Colour::None};
    bool motor_on_{false};
    bool sign_on_pending_{false};
    bool sign_off_pending_{false};
    bool running_{true};
    bool lamp_shows_link_{false};
    const Feature declared_;
    AlarmSettings settings_;
};

}  // namespace skyblip::common

#endif
