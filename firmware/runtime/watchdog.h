#ifndef SKYBLIP_RUNTIME_WATCHDOG_H
#define SKYBLIP_RUNTIME_WATCHDOG_H

#include <cstdint>

namespace skyblip::runtime {

// INFO: fc 20nov26 fed only while every supervised service checked in within its deadline
class FeedDecision {
   public:
    static constexpr int kMaxServices = 12;
    static constexpr int kNone = -1;

    int add(uint32_t deadline_ms) {
        if (count_ >= kMaxServices) return kNone;
        const int id = count_++;
        deadline_ms_[id] = deadline_ms;
        checked_in_ms_[id] = 0;
        return id;
    }

    void begin(uint32_t now_ms) {
        for (int i = 0; i < count_; i++) checked_in_ms_[i] = now_ms;
        started_ = true;
    }

    bool started() const { return started_; }

    void check_in(int service, uint32_t now_ms) {
        if (service < 0 || service >= count_) return;
        checked_in_ms_[service] = now_ms;
    }

    int stalled_service(uint32_t now_ms) const {
        if (!started_) return kNone;
        for (int i = 0; i < count_; i++)
            if (now_ms - checked_in_ms_[i] >= deadline_ms_[i]) return i;
        return kNone;
    }

    bool may_feed(uint32_t now_ms) const { return stalled_service(now_ms) == kNone; }

    uint32_t silent_ms(int service, uint32_t now_ms) const {
        return service >= 0 && service < count_ ? now_ms - checked_in_ms_[service] : 0;
    }

    int count() const { return count_; }

   private:
    uint32_t deadline_ms_[kMaxServices]{};
    uint32_t checked_in_ms_[kMaxServices]{};
    int count_{0};
    bool started_{false};
};

}  // namespace skyblip::runtime

#endif
