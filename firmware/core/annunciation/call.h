#ifndef SKYBLIP_CORE_ANNUNCIATION_CALL_H
#define SKYBLIP_CORE_ANNUNCIATION_CALL_H

#include <cstdint>
#include <optional>

#include "core/traffic/alarm.h"

namespace skyblip::annunciation {

struct Call {
    traffic::Level announcing{traffic::Level::None};
    traffic::Level undismissed{traffic::Level::None};
    // INFO: fc 17nov26 counts the leader's announcements since its boot, so a second aircraft is
    // new
    uint32_t announcement{0};
};

constexpr bool operator==(const Call& a, const Call& b) {
    return a.announcing == b.announcing && a.undismissed == b.undismissed &&
           a.announcement == b.announcement;
}

constexpr bool operator!=(const Call& a, const Call& b) { return !(a == b); }

class Follower {
   public:
    struct Heard {
        traffic::Level level{traffic::Level::None};
        traffic::Level undismissed{traffic::Level::None};
        bool announced{false};
    };

    Heard follow(const Call& call);
    // INFO: fc 17nov26 a press here silences the call it heard until the leader numbers a new one
    void dismiss();
    void lose();

    std::optional<uint32_t> dismissed() const { return dismissed_; }

   private:
    std::optional<uint32_t> heard_{};
    std::optional<uint32_t> dismissed_{};
};

}  // namespace skyblip::annunciation

#endif
