#ifndef SKYBLIP_RUNTIME_LOOP_H
#define SKYBLIP_RUNTIME_LOOP_H

#include "runtime/loop_meter.h"
#include "runtime/service.h"
#include "runtime/tasks.h"
#include "runtime/watchdog.h"

namespace skyblip::runtime {

class Loop {
   public:
    // names is a parallel array of literals, for the log line that says which
    // service went quiet. Optional: a loop with no names still supervises.
    Loop(Service* const* services, int count, const char* const* names = nullptr)
        : services_(services), count_(count) {
        for (int i = 0; i < count_; i++)
            feed_decision_.add(names != nullptr ? names[i] : "?", kServiceSilenceCeilingMs);
    }

    Status setup() {
        Status first = Status::Ok;
        for (int i = 0; i < count_; i++) {
            const Status status = services_[i]->setup();
            if (status != Status::Ok && first == Status::Ok) first = status;
        }
        return first;
    }

    void run_pass(uint32_t now_ms, LoopMeter* meter = nullptr) {
        // Registration happens at construction, before there is a time base to
        // stamp, so the deadlines start counting from the first pass.
        if (!feed_decision_.started()) feed_decision_.begin(now_ms);
        for (int i = 0; i < count_; i++) {
            if (meter != nullptr)
                meter->timed(static_cast<uint8_t>(i), [&] { services_[i]->tick(now_ms); });
            else
                services_[i]->tick(now_ms);
            if (services_[i]->progressing(now_ms)) feed_decision_.check_in(i, now_ms);
        }
    }

    bool may_feed_watchdog(uint32_t now_ms) const { return feed_decision_.may_feed(now_ms); }
    int stalled_service(uint32_t now_ms) const { return feed_decision_.stalled_service(now_ms); }
    const FeedDecision& feed_decision() const { return feed_decision_; }

   private:
    Service* const* services_;
    int count_;
    FeedDecision feed_decision_{};
};

}  // namespace skyblip::runtime

#endif
