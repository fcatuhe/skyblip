#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_ALARM_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_ALARM_H

#include "core/annunciation/pattern.h"
#include "core/diag/payload.h"
#include "core/indication/lamp.h"
#include "core/power/duty.h"
#include "core/traffic/alarm.h"
#include "core/traffic/formation.h"
#include "core/traffic/table.h"
#include "ports/indicator.h"
#include "products/skyblip_go/settings.h"
#include "runtime/service.h"
#include "runtime/tasks.h"

namespace skyblip::go {

// The one owner of everything this product says out loud or shows with a light.
// Nothing else calls alarm(), vibrate(), silence() or show(): the traffic tone,
// the first-fix chirp, the status lamp and the way down all go through the two
// policies in core/annunciation and core/indication, so each output has one
// driver and the rules that arbitrate are each written once.
//
// The lamp lives here rather than in PowerService, whose state most of it reads,
// because the highest-priority row in the table is the alarm level and because a
// device going down has to be silenced and darkened in the same breath - park()
// is already called at exactly the right point in the shutdown sequence.
class AlarmService : public runtime::Service {
   public:
    AlarmService(runtime::Context& context, const Settings& settings)
        : runtime::Service(context), settings_(settings) {}

    void tick(uint32_t now_ms) override;

    // The device is going down: the loop stops running, so the buzzer is
    // released and the lamp is darkened and let go here rather than left latched
    // on a rail that is about to drop.
    void park(uint32_t now_ms);

    void dismiss() { tracker_.dismiss(); }

    bool escalated_since_render() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

    traffic::Level announcing_level() const { return policy_.announcing_level(); }

    int formation_members() const { return formation_.members(); }
    bool sounding() const { return policy_.sounding(); }

    // What the table decided, and what the lamp is showing this instant. Both,
    // because they differ in the gaps of a wink and a test has to be able to say
    // which of the two it means.
    indication::Condition indicator_condition() const { return lamp_.condition(); }
    indication::Lamp lamp() const { return lamp_.lamp(); }

   private:
    bool silenced(traffic::Target& target, formation::State state,
                  const traffic::AlarmAssessment& assessment, uint32_t now_ms);
    void record_traffic(int slot, const traffic::Target& target,
                        const traffic::AlarmTracker::Decision& decision, uint32_t now_ms);
    formation::State watch_formation(traffic::Target& target, uint32_t now_ms);
    void drive(const annunciation::Situation& situation, uint32_t now_ms);
    void drive_lamp(uint32_t now_ms, bool running);
    void accrue_annunciator(uint32_t now_ms);
    void pulse_haptic();

    static constexpr uint16_t kHapticFeltThroughAHarnessMs = 400;

    // Eight passes through the shortest phase of the fastest pattern: the
    // cadence the ear gets is the cadence written in core/annunciation, to
    // within one pass of the loop.
    static constexpr uint32_t kPassesPerShortestPhase = 8;
    static_assert(runtime::kServiceStepMs * kPassesPerShortestPhase <=
                      annunciation::kShortestPhaseMs,
                  "the service loop is too coarse to resolve the advisory pair");

    // Three passes through the shortest flash in the table. An LED needs far less
    // time than a piezo to be seen, so the figure is smaller than the one above -
    // but a flash the loop cannot resolve is a flash nobody sees.
    static constexpr uint32_t kPassesPerShortestFlash = 3;
    static_assert(runtime::kServiceStepMs * kPassesPerShortestFlash <= indication::kShortestPhaseMs,
                  "the service loop is too coarse to resolve the shortest flash");

    power::OnTime sounding_{};
    uint32_t haptic_ms_{0};
    traffic::AlarmTracker tracker_{};
    formation::Tracker formation_{};
    uint32_t recorded_obs_ms_[traffic::TrafficTable::kCapacity]{};
    annunciation::Policy policy_{};
    indication::Policy lamp_{};
    bool dirty_{false};
    bool running_{true};
    const Settings& settings_;
};

}  // namespace skyblip::go

#endif
