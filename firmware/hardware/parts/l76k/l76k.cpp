#include "hardware/parts/l76k/l76k.h"

namespace skyblip::parts {

void L76k::send(const char* sentence, uint32_t now_ms) {
    size_t bytes = 0;
    while (sentence[bytes]) bytes++;
    uart_.write(reinterpret_cast<const uint8_t*>(sentence), bytes);
    last_command_ms_ = now_ms;
}

void L76k::start_sequence(uint32_t now_ms) {
    sequence_ = kCommands;
    sequence_count_ = kCommandCount;
    next_command_ = 0;
    cadence_ = Cadence::Second;
    satellites_in_view_every_ = 0;  // kCommands sets nGSV to 0, whatever the receiver was doing
    levels_heard_ = false;
    stage_ = ConfigStage::Sending;
    send_next(now_ms);
}

// INFO: fc 18nov26 only a part proven at 115200 is asked for 4 or 2 Hz, as V1.1 SS2.3.2 wants
void L76k::start_rate(Cadence cadence, uint32_t now_ms) {
    sequence_ = cadence == Cadence::Quarter ? kQuarterRateCommands : kHalfRateCommands;
    sequence_count_ = kQuarterRateCommandCount;
    next_command_ = 0;
    cadence_ = cadence;
    stage_ = ConfigStage::Sending;
    send_next(now_ms);
}

void L76k::settle(uint32_t now_ms) {
    stage_ = ConfigStage::Ready;
    begin_verify(now_ms);
}

// INFO: fc 18nov26 a refused 4 Hz is asked again every kQuarterRetryMs, and after a restart
void L76k::seek_quarters(uint32_t now_ms) {
    if (cadence_ == Cadence::Quarter || baud_rate() != kTargetBaudRate) return;
    if (quarter_refused_ && now_ms - refused_at_ms_ < kQuarterRetryMs) return;
    quarter_refused_ = false;
    rate_attempts_ = 1;
    start_rate(Cadence::Quarter, now_ms);
}

uint32_t L76k::solution_period_ms() const {
    switch (cadence_) {
        case Cadence::Quarter: return kSolutionPeriodMs;
        case Cadence::Half: return kHalfSolutionPeriodMs;
        case Cadence::Second: break;
    }
    return kFactorySolutionPeriodMs;
}

void L76k::send_next(uint32_t now_ms) {
    send(sequence_[next_command_++], now_ms);
    if (next_command_ < sequence_count_) return;
    stage_ = ConfigStage::Verifying;
    begin_verify(now_ms);
}

void L76k::begin_verify(uint32_t now_ms) {
    verify_start_ms_ = now_ms;
    verify_sentences_ = parser_.solution().sentences;
    verify_unstamped_ = unstamped_;
    verify_steps_ = cadence_steps_;
    unrequested_at_verify_ = parser_.unrequested();
}

bool L76k::verified() const {
    if (parser_.solution().sentences - verify_sentences_ < kMinVerifySentences || !obeying())
        return false;
    return cadence_ == Cadence::Second || on_cadence();
}

// INFO: fc 18nov26 a pair with no time is no solution to judge, so it never reads as off cadence
bool L76k::on_cadence() const {
    const uint32_t needed = cadence_ == Cadence::Quarter ? kMinQuarterSteps : kMinHalfSteps;
    return cadence_steps_ - verify_steps_ >= needed || unstamped_ != verify_unstamped_;
}

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

// INFO: fc 18nov26 a part that drifts off its rate while ready is asked for it again
void L76k::watch_cadence(uint32_t now_ms) {
    if (now_ms - verify_start_ms_ < kQuarterVerifyWindowMs) return;
    const bool heard = parser_.solution().sentences != verify_sentences_;
    const bool steady = on_cadence();
    begin_verify(now_ms);
    if (!heard || steady) return;
    rate_attempts_ = 1;
    start_rate(cadence_, now_ms);
}

// INFO: fc 18nov26 4 Hz refused is 2 Hz tried, then kCommands put the whole 1 Hz set back
void L76k::rate_failed(uint32_t now_ms) {
    if (rate_attempts_ < kMaxConfigAttempts) {
        rate_attempts_++;
        start_rate(cadence_, now_ms);
        return;
    }
    rate_attempts_ = 1;
    if (cadence_ == Cadence::Quarter) {
        quarter_refused_ = true;
        refused_at_ms_ = now_ms;
        start_rate(Cadence::Half, now_ms);
        return;
    }
    start_sequence(now_ms);
}

// INFO: fc 18nov26 what waited in the port before a configuration is no solution of this one
void L76k::flush() {
    uint8_t buffer[kReadChunkBytes];
    while (uart_.read(buffer, sizeof(buffer)) == sizeof(buffer)) {
    }
    parser_.reset();
    published_stamp_ms_ = -1;
}

// INFO: fc 19sep26 only a receiver that has just obeyed four $PCAS sentences is asked to move
bool L76k::raise_baud(uint32_t now_ms) {
    if (baud_rate() == kTargetBaudRate) return false;
    if (!port_can_retune()) return false;
    baud_before_raise_ = baud_rate();
    send(kBaudCommand, now_ms);
    baud_raises_++;
    return adopt_baud(kTargetBaudRate);
}

bool L76k::port_can_retune() { return rate_.set(baud_rate()); }

bool L76k::adopt_baud(uint32_t baud) {
    for (int i = 0; i < kBaudCandidateCount; i++) {
        if (kBaudCandidates[i] != baud) continue;
        if (!rate_.set(baud)) return false;
        baud_index_ = i;
        return true;
    }
    return false;
}

// One byte of nothing, then silence long enough for the receiver to have come
// up. Every attempt starts here, including the ones after a baud change: a
// receiver we could not hear may also have been asleep.
void L76k::begin_wake(uint32_t now_ms) {
    flush();
    const uint8_t wake = kWakeByte;
    uart_.write(&wake, 1);
    last_command_ms_ = now_ms;
    stage_ = ConfigStage::Waking;
}

bool L76k::next_baud() {
    if (baud_tried_ >= kBaudCandidateCount) return false;
    const int next = (baud_index_ + 1) % kBaudCandidateCount;
    if (!rate_.set(kBaudCandidates[next])) return false;  // this port cannot retune
    baud_index_ = next;
    baud_tried_++;
    return true;
}

void L76k::verify_failed(uint32_t now_ms) {
    if (cadence_ != Cadence::Second && parser_.solution().sentences != verify_sentences_) {
        rate_failed(now_ms);
        return;
    }
    // Two different failures wearing one face. A receiver that answered nothing
    // at all is a receiver we cannot hear, and the rate is the first suspect. A
    // receiver that is talking has the right rate and is simply not obeying, and
    // walking the baud rates would only lose the sentences we do get.
    const bool heard_nothing = parser_.solution().sentences == verify_sentences_;
    if (heard_nothing && next_baud()) {
        attempts_ = 1;
        begin_wake(now_ms);
        return;
    }
    if (attempts_ >= kMaxConfigAttempts) {
        stage_ = ConfigStage::Degraded;
        return;
    }
    attempts_++;
    begin_wake(now_ms);
}

void L76k::request_restart(ports::Restart kind) {
    pending_restart_ = static_cast<uint8_t>(kind);
    if (kind != ports::Restart::Hot) quarter_refused_ = false;
}

void L76k::service(uint32_t now_ms) {
    serviced_ms_ = now_ms;

    if (pending_restart_ != kNoRestart) {
        const bool factory = pending_restart_ == static_cast<uint8_t>(ports::Restart::Factory);
        send(kRestartCommands[pending_restart_], now_ms);
        pending_restart_ = kNoRestart;
        // A factory reset takes the constellations, the sentence set, the
        // dynamic model and the rate with it, so the receiver that comes back is
        // not the one we configured. A cold or warm start only throws away the
        // orbit data, which is the whole point of asking for one.
        if (factory) {
            validity_.reset();
            adopt_baud(kFactoryBaudRate);
            stage_ = ConfigStage::Restarting;
        }
        return;
    }

    switch (stage_) {
        case ConfigStage::Idle:
            attempts_ = 1;
            begin_wake(now_ms);
            break;
        case ConfigStage::Restarting:
            if (now_ms - last_command_ms_ < kRestartSettleMs) break;
            attempts_ = 1;
            begin_wake(now_ms);
            break;
        case ConfigStage::Waking:
            if (now_ms - last_command_ms_ < kWakeDelayMs) break;
            send(kIdentifyCommand, now_ms);
            stage_ = ConfigStage::Identifying;
            break;
        case ConfigStage::Identifying:
            // Either it named itself or it did not answer in time. It gets the
            // sequence regardless: the only alternative on this board is a
            // receiver left on its factory defaults.
            if (parser_.identified() || now_ms - last_command_ms_ >= kIdentifyWindowMs)
                start_sequence(now_ms);
            break;
        case ConfigStage::Sending:
            if (now_ms - last_command_ms_ >= kCommandGapMs) send_next(now_ms);
            break;
        case ConfigStage::Verifying:
            if (now_ms - verify_start_ms_ <
                (cadence_ != Cadence::Second ? kQuarterVerifyWindowMs : kVerifyWindowMs))
                break;
            if (!verified()) {
                verify_failed(now_ms);
                break;
            }
            if (raise_baud(now_ms)) {
                stage_ = ConfigStage::Confirming;
                begin_verify(now_ms);
            } else {
                settle(now_ms);
            }
            break;
        // INFO: fc 19sep26 a sentence that checksums at the new rate is the ack $PCAS01 never sends
        case ConfigStage::Confirming:
            if (parser_.solution().sentences != verify_sentences_) {
                settle(now_ms);
                break;
            }
            if (now_ms - verify_start_ms_ < kVerifyWindowMs) break;
            adopt_baud(baud_before_raise_);
            raise_fallbacks_++;
            stage_ = ConfigStage::Ready;
            break;
        case ConfigStage::Ready:
            seek_quarters(now_ms);
            if (stage_ == ConfigStage::Ready && cadence_ != Cadence::Second) watch_cadence(now_ms);
            break;
        case ConfigStage::Degraded: break;
    }

    if (stage_ == ConfigStage::Ready) pace_satellites_in_view(now_ms);
}

void L76k::pace_satellites_in_view(uint32_t now_ms) {
    const uint8_t every = satellites_in_view_every();
    if (every == satellites_in_view_every_) return;
    send(satellites_in_view_command(every), now_ms);
    satellites_in_view_every_ = every;
    if (every == 0) levels_heard_ = false;
}

// INFO: fc 29sep26 every solution until the first set lands, so the page still fills in a second
uint8_t L76k::satellites_in_view_every() const {
    if (!satellites_in_view_wanted_) return 0;
    if (!levels_heard_) return 1;
    return cadence_ == Cadence::Second ? kSatellitesInViewPeriodSolutions
                                       : kQuarterSatellitesInViewPeriodSolutions;
}

const char* L76k::satellites_in_view_command(uint8_t every) {
    switch (every) {
        case 0: return kSatellitesInViewOff;
        case 1: return kSatellitesInViewEverySolution;
        case kQuarterSatellitesInViewPeriodSolutions: return kSatellitesInViewQuarterPaced;
        default: return kSatellitesInViewPaced;
    }
}

// INFO: fc 13sep26 a $PCAS sentence is acknowledged only by what the receiver stops saying
bool L76k::obeying() const { return parser_.unrequested() == unrequested_at_verify_; }

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
