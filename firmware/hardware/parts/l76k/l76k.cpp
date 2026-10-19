#include "hardware/parts/l76k/l76k.h"

namespace skyblip::parts {

void L76k::send(const char* sentence, uint32_t now_ms) {
    size_t bytes = 0;
    while (sentence[bytes]) bytes++;
    uart_.write(reinterpret_cast<const uint8_t*>(sentence), bytes);
    last_command_ms_ = now_ms;
}

void L76k::start_sequence(uint32_t now_ms) {
    next_command_ = 0;
    satellites_in_view_every_ = 0;  // kCommands sets nGSV to 0, whatever the receiver was doing
    levels_heard_ = false;
    stage_ = ConfigStage::Sending;
    send_next(now_ms);
}

void L76k::send_next(uint32_t now_ms) {
    send(kCommands[next_command_++], now_ms);
    if (next_command_ < kCommandCount) return;
    stage_ = ConfigStage::Verifying;
    begin_verify(now_ms);
}

void L76k::begin_verify(uint32_t now_ms) {
    verify_start_ms_ = now_ms;
    verify_sentences_ = parser_.solution().sentences;
    unrequested_at_verify_ = parser_.unrequested();
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
            if (now_ms - verify_start_ms_ < kVerifyWindowMs) break;
            if (parser_.solution().sentences - verify_sentences_ < kMinVerifySentences ||
                !obeying()) {
                verify_failed(now_ms);
                break;
            }
            stage_ = raise_baud(now_ms) ? ConfigStage::Confirming : ConfigStage::Ready;
            begin_verify(now_ms);
            break;
        // INFO: fc 19sep26 a sentence that checksums at the new rate is the ack $PCAS01 never sends
        case ConfigStage::Confirming:
            if (parser_.solution().sentences != verify_sentences_) {
                stage_ = ConfigStage::Ready;
                break;
            }
            if (now_ms - verify_start_ms_ < kVerifyWindowMs) break;
            adopt_baud(baud_before_raise_);
            raise_fallbacks_++;
            stage_ = ConfigStage::Ready;
            break;
        case ConfigStage::Ready:
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
    return levels_heard_ ? kSatellitesInViewPeriodSolutions : 1;
}

const char* L76k::satellites_in_view_command(uint8_t every) {
    if (every == 0) return kSatellitesInViewOff;
    return every == 1 ? kSatellitesInViewEverySolution : kSatellitesInViewPaced;
}

// INFO: fc 13sep26 a $PCAS sentence is acknowledged only by what the receiver stops saying
bool L76k::obeying() const { return parser_.unrequested() == unrequested_at_verify_; }

bool L76k::poll(uint32_t now_ms) {
    uint8_t buffer[kReadChunkBytes];
    bool closed = false;
    for (;;) {
        size_t received = uart_.read(buffer, sizeof(buffer));
        if (received == 0) break;
        for (size_t i = 0; i < received; i++) {
            if (!parser_.feed(static_cast<char>(buffer[i]))) continue;
            const gnss::Sentence sentence = parser_.last_sentence();
            validity_.observe(parser_.solution(), sentence, now_ms);
            burst_levels_ = burst_levels_ ||
                            (sentence == gnss::Sentence::Gsv && satellites_in_view_every_ != 0);
            closed = closed || sentence == kBurstClosingSentence;
        }
        if (received < sizeof(buffer)) break;  // drained
    }
    // INFO: fc 29sep26 each burst: a GSV already on the wire lands after the $PCAS03 ending it
    if (closed && satellites_in_view_every_ == 0) parser_.forget_satellites_in_view();
    levels_heard_ = levels_heard_ || burst_levels_;

    const bool valid = validity_.check(now_ms) == gnss::FixReject::None;
    // A receiver that stops talking publishes nothing, so nothing would ever
    // withdraw the last fix it managed to send. The validity edge is an update in
    // its own right, and it is the one that matters most.
    if (!closed && valid == solution_.fix_valid) return false;

    levels_fresh_ = closed && burst_levels_ && satellites_in_view_every_ != 0;
    if (closed) burst_levels_ = false;
    solution_ = parser_.solution();
    solution_.fix_valid = valid;
    solution_.pps_latency_ms = pps_latency_ms();
    return true;
}

}  // namespace skyblip::parts
