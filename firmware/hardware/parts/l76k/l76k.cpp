#include "hardware/parts/l76k/l76k.h"

namespace skyblip::parts {

void L76k::send(const char* sentence, uint32_t now_ms) {
    size_t bytes = 0;
    while (sentence[bytes]) bytes++;
    uart_.write(reinterpret_cast<const uint8_t*>(sentence), bytes);
    last_command_ms_ = now_ms;
}

void L76k::wake(uint32_t now_ms) {
    const uint8_t wake = kWakeByte;
    uart_.write(&wake, 1);
    last_command_ms_ = now_ms;
}

void L76k::enter(ConfigStage stage, uint32_t now_ms) {
    if (stage == stage_) return;
    stage_ = stage;
    entered_ms_ = now_ms;
}

void L76k::start_sequence(uint32_t now_ms) {
    sequence_ = kCommands;
    sequence_count_ = kCommandCount;
    next_command_ = 0;
    cadence_ = Cadence::Second;
    satellites_in_view_every_ = 0;  // kCommands sets nGSV to 0, whatever the receiver was doing
    levels_heard_ = false;
    enter(ConfigStage::Sending, now_ms);
    send_next(now_ms);
}

// INFO: fc 18nov26 only a part proven at 115200 is asked for 2 Hz, as V1.1 SS2.3.2 wants
void L76k::start_rate(uint32_t now_ms) {
    sequence_ = kRateCommands;
    sequence_count_ = kRateCommandCount;
    next_command_ = 0;
    cadence_ = Cadence::Half;
    enter(ConfigStage::Sending, now_ms);
    send_next(now_ms);
}

void L76k::settle(uint32_t now_ms) {
    enter(ConfigStage::Ready, now_ms);
    begin_verify(now_ms);
}

// INFO: fc 19nov26 2 Hz refused thrice, or answered with silence, is not asked again until reboot
void L76k::seek_rate(uint32_t now_ms) {
    if (solution_rate_ == ports::SolutionRate::Second || half_refused_) return;
    if (cadence_ == Cadence::Half || baud_rate() != kTargetBaudRate) return;
    rate_attempts_ = 1;
    start_rate(now_ms);
}

void L76k::send_next(uint32_t now_ms) {
    send(sequence_[next_command_++], now_ms);
    if (next_command_ < sequence_count_) return;
    enter(ConfigStage::Verifying, now_ms);
    begin_verify(now_ms);
}

void L76k::begin_verify(uint32_t now_ms) {
    verify_start_ms_ = now_ms;
    verify_sentences_ = parser_.solution().sentences;
    verify_unstamped_ = unstamped_;
    verify_steps_ = cadence_steps_;
    unrequested_at_verify_ = parser_.unrequested();
}

// INFO: fc 18nov26 a part that drifts off its rate while ready is asked for it again
void L76k::watch_cadence(uint32_t now_ms) {
    if (now_ms - verify_start_ms_ < kRateVerifyWindowMs) return;
    const bool heard = parser_.solution().sentences != verify_sentences_;
    const bool steady = on_cadence();
    begin_verify(now_ms);
    if (!heard || steady) return;
    rate_attempts_ = 1;
    start_rate(now_ms);
}

// INFO: fc 19nov26 2 Hz refused thrice is the whole 1 Hz set put back
void L76k::rate_failed(uint32_t now_ms) {
    if (rate_attempts_ < kMaxConfigAttempts) {
        rate_attempts_++;
        start_rate(now_ms);
        return;
    }
    rate_attempts_ = 1;
    half_refused_ = true;
    start_sequence(now_ms);
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
    wake(now_ms);
    enter(ConfigStage::Waking, now_ms);
}

bool L76k::next_baud() {
    if (baud_tried_ >= kBaudCandidateCount) return false;
    if (!step_baud()) return false;
    baud_tried_++;
    return true;
}

bool L76k::step_baud() {
    const int next = (baud_index_ + 1) % kBaudCandidateCount;
    if (!rate_.set(kBaudCandidates[next])) return false;  // this port cannot retune
    baud_index_ = next;
    return true;
}

void L76k::verify_failed(uint32_t now_ms) {
    const bool heard_nothing = parser_.solution().sentences == verify_sentences_;
    if (cadence_ != Cadence::Second && !heard_nothing) {
        rate_failed(now_ms);
        return;
    }
    if (cadence_ != Cadence::Second) half_refused_ = true;
    // Two different failures wearing one face. A receiver that answered nothing
    // at all is a receiver we cannot hear, and the rate is the first suspect. A
    // receiver that is talking has the right rate and is simply not obeying, and
    // walking the baud rates would only lose the sentences we do get.
    if (heard_nothing && sentences() == 0 && next_baud()) {
        attempts_ = 1;
        begin_wake(now_ms);
        return;
    }
    if (heard_nothing) {
        start_probing(now_ms);
        return;
    }
    if (attempts_ >= kMaxConfigAttempts) {
        enter(ConfigStage::Degraded, now_ms);
        return;
    }
    attempts_++;
    begin_wake(now_ms);
}

void L76k::request_restart(ports::Restart kind) { pending_restart_ = static_cast<uint8_t>(kind); }

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
            enter(ConfigStage::Restarting, now_ms);
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
            enter(ConfigStage::Identifying, now_ms);
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
                (cadence_ != Cadence::Second ? kRateVerifyWindowMs : kVerifyWindowMs))
                break;
            if (!verified()) {
                verify_failed(now_ms);
                break;
            }
            if (raise_baud(now_ms)) {
                enter(ConfigStage::Confirming, now_ms);
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
            enter(ConfigStage::Ready, now_ms);
            break;
        case ConfigStage::Ready:
            if (silent(now_ms)) {
                start_probing(now_ms);
                break;
            }
            seek_rate(now_ms);
            if (stage_ == ConfigStage::Ready && cadence_ != Cadence::Second) watch_cadence(now_ms);
            break;
        case ConfigStage::Degraded:
            if (silent(now_ms)) start_probing(now_ms);
            break;
        case ConfigStage::Probing: probe(now_ms); break;
        case ConfigStage::Resetting: hold_reset(now_ms); break;
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
                                       : kHalfSecondSatellitesInViewPeriodSolutions;
}

const char* L76k::satellites_in_view_command(uint8_t every) {
    switch (every) {
        case 0: return kSatellitesInViewOff;
        case 1: return kSatellitesInViewEverySolution;
        case kHalfSecondSatellitesInViewPeriodSolutions: return kSatellitesInViewHalfSecondPaced;
        default: return kSatellitesInViewPaced;
    }
}

}  // namespace skyblip::parts
