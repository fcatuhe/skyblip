#include "hardware/parts/l76k/l76k.h"

namespace skyblip::parts {

bool L76k::verified() const {
    if (window_sentences() < kMinVerifySentences || !obeying()) return false;
    return cadence_ == Cadence::Second || on_cadence();
}

// INFO: fc 18nov26 a pair with no time is no solution to judge, so it never reads as off cadence
bool L76k::on_cadence() const {
    return window_steps() >= kMinHalfSteps || unstamped_ != verify_unstamped_;
}

// INFO: fc 13sep26 a $PCAS sentence is acknowledged only by what the receiver stops saying
bool L76k::obeying() const { return parser_.unrequested() == unrequested_at_verify_; }

// INFO: fc 18nov26 a step is two pairs stamped one period apart, each on that period's grid
void L76k::observe_cadence() {
    const int32_t stamp_ms = parser_.stamp_ms();
    if (stamp_ms < 0) {
        unstamped_++;
        last_stamp_ms_ = -1;
        return;
    }
    const int32_t period_ms = static_cast<int32_t>(solution_period_ms());
    const int32_t step_ms = (stamp_ms - last_stamp_ms_ + kDayMs) % kDayMs;
    if (last_stamp_ms_ >= 0 && step_ms == period_ms && stamp_ms % period_ms == 0) cadence_steps_++;
    last_stamp_ms_ = stamp_ms;
}

uint32_t L76k::solution_period_ms() const {
    return cadence_ == Cadence::Half ? kSolutionPeriodMs : kFactorySolutionPeriodMs;
}

// INFO: fc 18nov26 what waited in the port before a configuration is no solution of this one
void L76k::flush() {
    uint8_t buffer[kReadChunkBytes];
    while (uart_.read(buffer, sizeof(buffer)) == sizeof(buffer)) {
    }
    parser_.reset();
    published_stamp_ms_ = -1;
}

bool L76k::poll(uint32_t now_ms) {
    uint8_t buffer[kReadChunkBytes];
    gnss::GnssSolution paired{};
    bool paired_valid = false;
    int32_t paired_stamp_ms = -1;
    int pairs = 0;
    for (;;) {
        size_t received = uart_.read(buffer, sizeof(buffer));
        if (received == 0) break;
        for (size_t i = 0; i < received; i++) {
            if (!parser_.feed(static_cast<char>(buffer[i]))) continue;
            const gnss::Sentence sentence = parser_.last_sentence();
            validity_.observe(parser_.solution(), sentence, now_ms);
            burst_levels_ = burst_levels_ ||
                            (sentence == gnss::Sentence::Gsv && satellites_in_view_every_ != 0);
            if (sentence != kBurstClosingSentence || !parser_.same_epoch()) continue;
            observe_cadence();
            pairs++;
            paired = parser_.solution();
            paired_valid = validity_.evaluate(now_ms) == gnss::FixReject::None;
            paired_stamp_ms = parser_.stamp_ms();
        }
        if (received < sizeof(buffer)) break;  // drained
    }
    if (sentences() != heard_sentences_) {
        heard_sentences_ = sentences();
        heard_ms_ = now_ms;
    }
    const bool closed = pairs > 0 && newer_than_published(paired_stamp_ms);
    if (closed && paired_stamp_ms >= 0) published_stamp_ms_ = paired_stamp_ms;
    // INFO: fc 29sep26 each burst: a GSV already on the wire lands after the $PCAS03 ending it
    if (closed && satellites_in_view_every_ == 0) parser_.forget_satellites_in_view();
    levels_heard_ = levels_heard_ || burst_levels_;

    const bool valid = validity_.check(now_ms) == gnss::FixReject::None;
    // INFO: fc 18nov26 only a whole GGA and RMC pair is published, a withdrawal needs none
    if (!closed) {
        if (valid || !solution_.fix_valid) return false;
        levels_fresh_ = false;
        solution_.fix_valid = false;
        return true;
    }

    levels_fresh_ = burst_levels_ && satellites_in_view_every_ != 0;
    burst_levels_ = false;
    solution_ = paired;
    solution_.fix_valid = paired_valid && valid;
    solution_.pps_latency_ms = pps_latency_ms();
    solution_.backlogged = pairs > 1;
    solution_.delivery_ms =
        static_cast<uint16_t>(kBurstStartMs + io::wire_ms(kBurstBytes, baud_rate()));
    return true;
}

// INFO: fc 18nov26 a pair at or just behind the last one published is a replay, a far jump not
bool L76k::newer_than_published(int32_t stamp_ms) const {
    if (stamp_ms < 0 || published_stamp_ms_ < 0) return true;
    const int32_t behind_ms = (published_stamp_ms_ - stamp_ms + kDayMs) % kDayMs;
    return behind_ms > static_cast<int32_t>(gnss::kSentenceMaxAgeMs) || behind_ms > kDayMs / 2;
}

}  // namespace skyblip::parts
