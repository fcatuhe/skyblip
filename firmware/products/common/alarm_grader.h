#ifndef SKYBLIP_PRODUCTS_COMMON_ALARM_GRADER_H
#define SKYBLIP_PRODUCTS_COMMON_ALARM_GRADER_H

#include <cstdint>
#include <optional>

#include "core/annunciation/call.h"
#include "core/traffic/alarm.h"
#include "core/traffic/formation.h"
#include "core/traffic/table.h"
#include "products/common/alarm.h"
#include "runtime/service.h"

namespace skyblip::common {

struct Announcement {
    traffic::Level level{traffic::Level::None};
    bool announced{false};
};

class AlarmGrader {
   public:
    explicit AlarmGrader(runtime::Context& context) : context_(context) {}

    Announcement grade(Alarm alarm, uint32_t now_ms);

    void dismiss() {
        tracker_.dismiss();
        follower_.dismiss();
    }
    // INFO: fc 17nov26 a press on the paired Bravo, honoured only for the call Bravo had heard
    void dismiss_announcement(uint32_t announcement) {
        if (announcement == call_.announcement) tracker_.dismiss();
    }
    std::optional<uint32_t> dismissed_announcement() const { return follower_.dismissed(); }

    int formation_members() const { return formation_.members(); }

   private:
    formation::State watch_formation(traffic::Target& target, uint32_t now_ms);
    bool silenced(traffic::Target& target, formation::State state);
    void record_traffic(int slot, const traffic::Target& target,
                        const traffic::AlarmTracker::Decision& decision, uint32_t now_ms);

    runtime::Context& context_;
    traffic::AlarmTracker tracker_{};
    annunciation::Call call_{};
    annunciation::Follower follower_{};
    formation::Tracker formation_{};
    uint32_t recorded_observation_ms_[traffic::TrafficTable::kMaxTargets]{};
};

}  // namespace skyblip::common

#endif
