#include "protocols/adsl/transmit.h"

#include <algorithm>

namespace skyblip::adsl {

namespace {
uint32_t hash(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}
}  // namespace

uint32_t ground_second(uint32_t address) { return hash(address) % kGroundReportPeriodS; }

bool on_schedule(uint32_t address, uint32_t utc_s, bool airborne) {
    return utc_s % period_s(airborne) == (airborne ? 0u : ground_second(address));
}

int instant_between(uint32_t address, uint32_t utc_s, int first, int last) {
    if (last <= first) return first;
    // Inclusive of last: a burst starting there still ends inside the slack.
    return first +
           static_cast<int>(hash(address ^ hash(utc_s)) % static_cast<uint32_t>(last - first + 1));
}

int status_instant_between(uint32_t address, uint32_t utc_s, int first, int last, int apart_ms) {
    const int position = instant_between(address, utc_s, first, last);
    const int blocked_from = std::max(first, position - apart_ms + 1);
    const int blocked = std::min(last, position + apart_ms - 1) - blocked_from + 1;
    const int free = last - first + 1 - blocked;
    if (free <= 0) return first;
    const int drawn =
        first + static_cast<int>(hash(hash(address ^ hash(utc_s))) % static_cast<uint32_t>(free));
    return drawn < blocked_from ? drawn : drawn + blocked;
}

void AirTime::spend(uint32_t now_ms, uint32_t air_ms) {
    if (!started_) {
        started_ = true;
        head_start_ms_ = now_ms;
    } else if (now_ms - head_start_ms_ >= kWindowMs && head_start_ms_ - now_ms >= kWindowMs) {
        // A gap wider than the window in either direction: nothing the ring holds
        // is inside the hour any more. Only one of the two differences can be
        // under the window, so an instant a little BEHIND the head is a caller
        // repeating a pass, not a gap, and it lands in the head bucket below.
        for (uint8_t i = 0; i < kBuckets; i++) bucket_ms_[i] = 0;
        head_ = 0;
        head_start_ms_ = now_ms;
    } else {
        while (now_ms - head_start_ms_ >= kBucketMs && now_ms - head_start_ms_ < kWindowMs) {
            head_ = static_cast<uint8_t>(head_ + 1 == kBuckets ? 0 : head_ + 1);
            bucket_ms_[head_] = 0;
            head_start_ms_ += kBucketMs;
        }
    }
    bucket_ms_[head_] += air_ms;
    total_ms_ += air_ms;
    bursts_++;
}

uint32_t AirTime::window_ms(uint32_t now_ms) const {
    if (!started_) return 0;
    uint32_t sum = 0;
    uint32_t start_ms = head_start_ms_;
    for (uint8_t back = 0; back < kBuckets; back++) {
        const uint8_t at = static_cast<uint8_t>((head_ + kBuckets - back) % kBuckets);
        if (now_ms - start_ms < kWindowMs) sum += bucket_ms_[at];
        start_ms -= kBucketMs;
    }
    return sum;
}

}
