#include "hardware/parts/l76k/l76k.h"

namespace skyblip::parts {

void L76k::send(const char* sentence, uint32_t now_ms) {
    size_t len = 0;
    while (sentence[len]) len++;
    uart_.write(reinterpret_cast<const uint8_t*>(sentence), len);
    last_command_ms_ = now_ms;
}

void L76k::start_sequence(uint32_t now_ms) {
    next_command_ = 0;
    gsv_on_ = false;  // kCommands sets nGSV to 0, whatever the receiver was doing
    state_ = Config::Sending;
    send_next(now_ms);
}

void L76k::send_next(uint32_t now_ms) {
    send(kCommands[next_command_++], now_ms);
    if (next_command_ < kCommandCount) return;
    state_ = Config::Verifying;
    begin_verify(now_ms);
}

void L76k::begin_verify(uint32_t now_ms) {
    verify_start_ms_ = now_ms;
    verify_updates_ = parser_.solution().updates;
    verify_unrequested_ = parser_.unrequested();
}

// INFO: fc 19sep26 only a receiver that has just obeyed four $PCAS sentences is asked to move
bool L76k::raise_baud(uint32_t now_ms) {
    if (baud_rate() == kTargetBaudRate) return false;
    if (!port_can_retune()) return false;
    baud_before_raise_ = baud_rate();
    send(kBaudCommand, now_ms);
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
    const uint8_t wake = kWakeByte;
    uart_.write(&wake, 1);
    last_command_ms_ = now_ms;
    state_ = Config::Waking;
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
    // Two different failures wearing one face. A receiver that answered nothing
    // at all is a receiver we cannot hear, and the rate is the first suspect. A
    // receiver that is talking has the right rate and is simply not obeying, and
    // walking the baud rates would only lose the sentences we do get.
    const bool heard_nothing = parser_.solution().updates == verify_updates_;
    if (heard_nothing && next_baud()) {
        attempts_ = 1;
        begin_wake(now_ms);
        return;
    }
    if (attempts_ >= kMaxConfigAttempts) {
        state_ = Config::Degraded;
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
            adopt_baud(kBaudRate);
            state_ = Config::Restarting;
        }
        return;
    }

    switch (state_) {
        case Config::Idle:
            attempts_ = 1;
            begin_wake(now_ms);
            break;
        case Config::Restarting:
            if (now_ms - last_command_ms_ < kRestartSettleMs) break;
            attempts_ = 1;
            begin_wake(now_ms);
            break;
        case Config::Waking:
            if (now_ms - last_command_ms_ < kWakeDelayMs) break;
            send(kIdentifyCommand, now_ms);
            state_ = Config::Identifying;
            break;
        case Config::Identifying:
            // Either it named itself or it did not answer in time. It gets the
            // sequence regardless: the only alternative on this board is a
            // receiver left on its factory defaults.
            if (parser_.identified() || now_ms - last_command_ms_ >= kIdentifyWindowMs)
                start_sequence(now_ms);
            break;
        case Config::Sending:
            if (now_ms - last_command_ms_ >= kCommandGapMs) send_next(now_ms);
            break;
        case Config::Verifying:
            if (now_ms - verify_start_ms_ < kVerifyWindowMs) break;
            if (parser_.solution().updates - verify_updates_ < kMinVerifyUpdates || !obeying()) {
                verify_failed(now_ms);
                break;
            }
            state_ = raise_baud(now_ms) ? Config::Confirming : Config::Ready;
            begin_verify(now_ms);
            break;
        // INFO: fc 19sep26 a sentence that checksums at the new rate is the ack $PCAS01 never sends
        case Config::Confirming:
            if (parser_.solution().updates != verify_updates_) {
                state_ = Config::Ready;
                break;
            }
            if (now_ms - verify_start_ms_ < kVerifyWindowMs) break;
            adopt_baud(baud_before_raise_);
            state_ = Config::Ready;
            break;
        case Config::Ready:
        case Config::Degraded: break;
    }

    if (state_ == Config::Ready && gsv_on_ != gsv_wanted_) {
        send(gsv_wanted_ ? kSatellitesInViewOn : kSatellitesInViewOff, now_ms);
        gsv_on_ = gsv_wanted_;
    }
}

// INFO: fc 13sep26 a $PCAS sentence is acknowledged only by what the receiver stops saying
bool L76k::obeying() const { return parser_.unrequested() == verify_unrequested_; }

bool L76k::poll(uint32_t now_ms) {
    uint8_t buf[kChunk];
    bool closed = false;
    for (;;) {
        size_t n = uart_.read(buf, sizeof(buf));
        if (n == 0) break;
        for (size_t i = 0; i < n; i++) {
            if (!parser_.feed(static_cast<char>(buf[i]))) continue;
            validity_.observe(parser_.solution(), parser_.last_sentence(), now_ms);
            closed = closed || parser_.last_sentence() == kBurstClosingSentence;
        }
        if (n < sizeof(buf)) break;  // drained
    }

    const bool valid = validity_.check(now_ms) == gnss::FixReject::None;
    // A receiver that stops talking publishes nothing, so nothing would ever
    // withdraw the last fix it managed to send. The validity edge is an update in
    // its own right, and it is the one that matters most.
    if (!closed && valid == solution_.fix_valid) return false;

    solution_ = parser_.solution();
    solution_.fix_valid = valid;
    solution_.pps_latency_ms = pps_latency_ms();
    return true;
}

}  // namespace skyblip::parts
