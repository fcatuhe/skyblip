#ifndef SKYBLIP_RUNTIME_LOOP_H
#define SKYBLIP_RUNTIME_LOOP_H

#include "runtime/loop_meter.h"
#include "runtime/service.h"
#include "runtime/tasks.h"
#include "runtime/watchdog.h"

namespace skyblip::runtime {

class Loop {
   public:
    struct Entry {
        Service* service;
        diagnostics::LoopService code;
    };

    Loop(const Entry* entries, int count) : entries_(entries), count_(count) {
        for (int i = 0; i < count_; i++) feed_decision_.add(kServiceSilenceCeilingMs);
    }

    Status setup() {
        Status first = Status::Ok;
        for (int i = 0; i < count_; i++) {
            const Status status = entries_[i].service->setup();
            if (status != Status::Ok && first == Status::Ok) first = status;
        }
        return first;
    }

    void run_pass(uint32_t now_ms, LoopMeter* meter = nullptr) {
        // Registration happens at construction, before there is a time base to
        // stamp, so the deadlines start counting from the first pass.
        if (!feed_decision_.started()) feed_decision_.begin(now_ms);
        for (int i = 0; i < count_; i++) {
            Service& service = *entries_[i].service;
            if (meter != nullptr)
                meter->timed(entries_[i].code, [&] { service.tick(now_ms); });
            else
                service.tick(now_ms);
            if (service.progressing(now_ms)) feed_decision_.check_in(i, now_ms);
        }
    }

    bool may_feed_watchdog(uint32_t now_ms) const { return feed_decision_.may_feed(now_ms); }
    int stalled_service(uint32_t now_ms) const { return feed_decision_.stalled_service(now_ms); }

   private:
    const Entry* entries_;
    int count_;
    FeedDecision feed_decision_{};
};

}  // namespace skyblip::runtime

#endif
