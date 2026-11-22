#ifndef SKYBLIP_RUNTIME_LOOP_H
#define SKYBLIP_RUNTIME_LOOP_H

#include <algorithm>

#include "runtime/loop_meter.h"
#include "runtime/service.h"
#include "runtime/tasks.h"
#include "runtime/watchdog.h"

namespace skyblip::runtime {

struct LoopEntry {
    Service* service;
    diagnostics::LoopService code;
};

template <int N>
class Loop {
   public:
    explicit Loop(const LoopEntry (&entries)[N]) : entries_(entries) {}

    Status setup() {
        Status first = Status::Ok;
        for (int i = 0; i < N; i++) {
            const Status status = entries_[i].service->setup();
            if (status != Status::Ok && first == Status::Ok) first = status;
        }
        return first;
    }

    void run_pass(uint32_t now_ms, LoopMeter* meter = nullptr) {
        if (!feed_decision_.started()) feed_decision_.begin(now_ms);
        for (int i = 0; i < N; i++) {
            Service& service = *entries_[i].service;
            if (meter != nullptr)
                meter->timed(entries_[i].code, [&] { service.tick(now_ms); });
            else
                service.tick(now_ms);
            if (service.progressing(now_ms)) feed_decision_.check_in(i, now_ms);
        }
    }

    uint32_t due_in_ms(uint32_t now_ms) const {
        uint32_t due_ms = kServiceRestCeilingMs;
        for (int i = 0; i < N; i++)
            due_ms = std::min(due_ms, entries_[i].service->due_in_ms(now_ms));
        return due_ms;
    }

    bool may_feed_watchdog(uint32_t now_ms) const { return feed_decision_.may_feed(now_ms); }
    int stalled_service(uint32_t now_ms) const { return feed_decision_.stalled_service(now_ms); }

   private:
    const LoopEntry* entries_;
    FeedDecision<N> feed_decision_{};
};

}  // namespace skyblip::runtime

#endif
