// runtime/watchdog.h: the loop's feed decision, as pure logic.
//
// A watchdog fed unconditionally from the bottom of a loop proves the loop is
// running and nothing else. This is the other half: every supervised service
// checks in when it has made progress, and the dog is fed only while all of them
// are inside their deadline. A service that has gone quiet takes the device down
// with it, which on a tracker is the correct outcome and the only one that shows
// up in the reset reason.
#ifndef SKYBLIP_RUNTIME_WATCHDOG_H
#define SKYBLIP_RUNTIME_WATCHDOG_H

#include <cstdint>

namespace skyblip::runtime {

class FeedDecision {
   public:
    static constexpr int kMaxServices = 12;
    static constexpr int kNone = -1;

    // Register a supervised service. The name is a literal owned by the caller: no
    // copy, no allocation. Returns the service id, or kNone when full.
    int add(const char* name, uint32_t deadline_ms) {
        if (count_ >= kMaxServices) return kNone;
        const int id = count_++;
        name_[id] = name;
        deadline_ms_[id] = deadline_ms;
        last_ms_[id] = 0;
        return id;
    }

    // Start the clock on every registered service. Called once, when the loop first
    // runs: registration happens before there is a time base to stamp.
    void begin(uint32_t now_ms) {
        for (int i = 0; i < count_; i++) last_ms_[i] = now_ms;
        started_ = true;
    }

    bool started() const { return started_; }

    void check_in(int service, uint32_t now_ms) {
        if (service < 0 || service >= count_) return;
        last_ms_[service] = now_ms;
    }

    // The first service past its deadline, or kNone. First rather than worst: it is
    // the one to name in the log, and there is usually only one.
    int stalled_service(uint32_t now_ms) const {
        if (!started_) return kNone;
        for (int i = 0; i < count_; i++)
            if (now_ms - last_ms_[i] >= deadline_ms_[i]) return i;
        return kNone;
    }

    bool may_feed(uint32_t now_ms) const { return stalled_service(now_ms) == kNone; }

    const char* name(int service) const {
        return service >= 0 && service < count_ ? name_[service] : "";
    }

    uint32_t silent_ms(int service, uint32_t now_ms) const {
        return service >= 0 && service < count_ ? now_ms - last_ms_[service] : 0;
    }

    int count() const { return count_; }

   private:
    const char* name_[kMaxServices]{};
    uint32_t deadline_ms_[kMaxServices]{};
    uint32_t last_ms_[kMaxServices]{};
    int count_{0};
    bool started_{false};
};

}  // namespace skyblip::runtime

#endif
