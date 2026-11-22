#ifndef SKYBLIP_RUNTIME_WATCHDOG_H
#define SKYBLIP_RUNTIME_WATCHDOG_H

#include <cstdint>

#include "runtime/tasks.h"

namespace skyblip::runtime {

// INFO: fc 20nov26 fed only while every service checked in within kServiceSilenceCeilingMs
template <int N>
class FeedDecision {
   public:
    static constexpr int kNone = -1;

    void begin(uint32_t now_ms) {
        for (uint32_t& checked_in_ms : checked_in_ms_) checked_in_ms = now_ms;
        started_ = true;
    }

    bool started() const { return started_; }

    void check_in(int service, uint32_t now_ms) { checked_in_ms_[service] = now_ms; }

    int stalled_service(uint32_t now_ms) const {
        if (!started_) return kNone;
        for (int i = 0; i < N; i++)
            if (now_ms - checked_in_ms_[i] >= kServiceSilenceCeilingMs) return i;
        return kNone;
    }

    bool may_feed(uint32_t now_ms) const { return stalled_service(now_ms) == kNone; }

   private:
    uint32_t checked_in_ms_[N]{};
    bool started_{false};
};

}  // namespace skyblip::runtime

#endif
