#include "hardware/parts/ag3335/ag3335.h"

namespace skyblip::parts {

void Ag3335::request_restart(ports::Restart kind) { pending_restart_ = static_cast<uint8_t>(kind); }

// INFO: fc 18nov26 a rate it cannot solve at leaves it unconfigured and degraded, never at 1 Hz
void Ag3335::service(uint32_t now_ms) {
    serviced_ms_ = now_ms;
    // INFO: fc 26nov26 the receiver powered up with the board, so its first switch waits from here
    if (!started_) {
        started_ = true;
        power_switched_ms_ = now_ms;
    }
    if (!solves(solution_rate_)) {
        stage_ = ConfigStage::Degraded;
        return;
    }

    if (pending_restart_ != kNoRestart) {
        const bool factory = pending_restart_ == static_cast<uint8_t>(ports::Restart::Factory);
        const char* command = kRestartCommands[pending_restart_];
        pending_restart_ = kNoRestart;
        if (!factory) {
            send(command);
            return;
        }
        validity_.reset();
        await(command, now_ms);
        stage_ = ConfigStage::Restarting;
        return;
    }

    switch (stage_) {
        case ConfigStage::Idle:
            attempts_ = 0;
            identify(now_ms);
            break;
        case ConfigStage::Restarting:
            if (!ack_heard_ && now_ms - last_command_ms_ < kAckWindowMs) break;
            attempts_ = 0;
            identify(now_ms);
            break;
        case ConfigStage::Identifying:
            if (setting_heard_) {
                start_sequence(now_ms);
                break;
            }
            if (now_ms - last_command_ms_ < kAckWindowMs) break;
            if (attempts_ < kCommandAttempts)
                identify(now_ms);
            else
                start_sequence(now_ms);
            break;
        case ConfigStage::Sending:
            if (holding_) {
                if (now_ms - power_switched_ms_ >= kPowerSwitchHoldMs) send_command(now_ms);
                break;
            }
            if (ack_heard_) {
                ack_heard_ = false;
                if (ack_result_ == PairResult::Success || switch_missing()) {
                    next_command(now_ms);
                } else if (ack_result_ == PairResult::Processing) {
                    processing_ = true;
                } else {
                    retry_command(now_ms);
                }
                break;
            }
            if (now_ms - last_command_ms_ >= (processing_ ? kProcessingWindowMs : kAckWindowMs))
                retry_command(now_ms);
            break;
        case ConfigStage::Waking:
        case ConfigStage::Verifying:
        case ConfigStage::Confirming:
        case ConfigStage::Ready:
        case ConfigStage::Degraded:
        case ConfigStage::Probing:
        case ConfigStage::Resetting: break;
    }
}

void Ag3335::identify(uint32_t now_ms) {
    setting_heard_ = false;
    await(kIdentifyCommand, now_ms);
    stage_ = ConfigStage::Identifying;
}

void Ag3335::start_sequence(uint32_t now_ms) {
    plan_sequence();
    next_command_ = 0;
    attempts_ = 0;
    sequence_refused_ = false;
    stage_ = ConfigStage::Sending;
    send_command(now_ms);
}

// INFO: fc 26nov26 the query follows the sleep lock, which LC29H V1.4 §2.4.29 asks before $PAIR003
void Ag3335::plan_sequence() {
    plan_count_ = 0;
    band_switched_ = false;
    band_ = reader_.frequency() == 'S' ? ports::GnssBand::L1 : ports::GnssBand::Unknown;
    for (const char* command : kCommands) {
        plan_[plan_count_++] = command;
        if (command == kLockSleep && reader_.frequency() == 'D')
            plan_[plan_count_++] = kDualBandQuery;
    }
}

void Ag3335::send_command(uint32_t now_ms) {
    const char* command = plan_[next_command_];
    holding_ = power_switch(command) && now_ms - power_switched_ms_ < kPowerSwitchHoldMs;
    if (holding_) return;
    if (command == kDualBandQuery) band_answered_ = false;
    await(command, now_ms);
}

void Ag3335::next_command(uint32_t now_ms) {
    const char* done = plan_[next_command_];
    if (done == kDualBandQuery) settle_band();
    if (power_switch(done)) power_switched_ms_ = now_ms;
    next_command_++;
    attempts_ = 0;
    if (next_command_ < plan_count_) {
        send_command(now_ms);
        return;
    }
    stage_ = sequence_refused_ ? ConfigStage::Degraded : ConfigStage::Ready;
}

// INFO: fc 26nov26 a build that does not answer $PAIR105 leaves the band unknown
void Ag3335::settle_band() {
    if (!band_answered_) return;
    if (reader_.dual_band_on()) {
        band_ = ports::GnssBand::L1L5;
        return;
    }
    if (band_switched_) {
        band_ = ports::GnssBand::L1;
        return;
    }
    band_switched_ = true;
    for (int i = plan_count_ - 1; i > next_command_; i--) plan_[i + kBandCommandCount] = plan_[i];
    for (int i = 0; i < kBandCommandCount; i++) plan_[next_command_ + 1 + i] = kBandCommands[i];
    plan_count_ += kBandCommandCount;
}

bool Ag3335::power_switch(const char* sentence) {
    return sentence == kGnssOff || sentence == kGnssOn;
}

bool Ag3335::switch_missing() const {
    return awaited_id_ == kDualBandQueryId && ack_result_ == PairResult::Unsupported;
}

// INFO: fc 11nov26 a refused command is skipped, so no later setting stays on its default
void Ag3335::retry_command(uint32_t now_ms) {
    if (attempts_ < kCommandAttempts) {
        send_command(now_ms);
        return;
    }
    sequence_refused_ = true;
    next_command(now_ms);
}

void Ag3335::await(const char* sentence, uint32_t now_ms) {
    awaited_id_ = pair_command_id(sentence);
    ack_heard_ = false;
    processing_ = false;
    attempts_++;
    last_command_ms_ = now_ms;
    send(sentence);
}

void Ag3335::send(const char* sentence) {
    size_t bytes = 0;
    while (sentence[bytes]) bytes++;
    uart_.write(reinterpret_cast<const uint8_t*>(sentence), bytes);
}

// TODO: fc 11nov26 an ack names no arguments, so a late one can credit the next $PAIR062 until #169
void Ag3335::hear(PairReader::Heard heard) {
    if (heard == PairReader::Heard::Setting) setting_heard_ = true;
    if (heard == PairReader::Heard::DualBand && awaited_id_ == kDualBandQueryId) {
        band_answered_ = true;
        ack_heard_ = true;
        ack_result_ = PairResult::Success;
        return;
    }
    if (heard != PairReader::Heard::Ack || reader_.ack_command() != awaited_id_) return;
    // INFO: fc 26nov26 the query is answered by its reply, which follows the acknowledgement
    if (awaited_id_ == kDualBandQueryId && reader_.ack_result() == PairResult::Success) return;
    ack_heard_ = true;
    ack_result_ = reader_.ack_result();
}

bool Ag3335::poll(uint32_t now_ms) {
    if (!solves(solution_rate_)) return false;
    uint8_t buffer[kReadChunkBytes];
    bool closed = false;
    for (;;) {
        const size_t received = uart_.read(buffer, sizeof(buffer));
        if (received == 0) break;
        for (size_t i = 0; i < received; i++) {
            const char character = static_cast<char>(buffer[i]);
            hear(reader_.feed(character));
            if (!parser_.feed(character)) continue;
            const gnss::Sentence sentence = parser_.last_sentence();
            validity_.observe(parser_.solution(), sentence, now_ms);
            closed = closed || sentence == kBurstClosingSentence;
        }
        if (received < sizeof(buffer)) break;
    }

    const bool valid = validity_.check(now_ms) == gnss::FixReject::None;
    if (!closed && valid == solution_.fix_valid) return false;

    solution_ = parser_.solution();
    solution_.fix_valid = valid;
    solution_.pps_latency_ms = pps_latency_ms();
    return true;
}

}  // namespace skyblip::parts
