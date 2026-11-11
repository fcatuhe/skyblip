#include "hardware/parts/ag3335/ag3335.h"

namespace skyblip::parts {

void Ag3335::request_restart(ports::Restart kind) { pending_restart_ = static_cast<uint8_t>(kind); }

void Ag3335::service(uint32_t now_ms) {
    serviced_ms_ = now_ms;

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
            if (ack_heard_) {
                ack_heard_ = false;
                if (ack_result_ == PairResult::Success) {
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
        case ConfigStage::Degraded: break;
    }
}

void Ag3335::identify(uint32_t now_ms) {
    setting_heard_ = false;
    await(kIdentifyCommand, now_ms);
    stage_ = ConfigStage::Identifying;
}

void Ag3335::start_sequence(uint32_t now_ms) {
    next_command_ = 0;
    attempts_ = 0;
    sequence_refused_ = false;
    stage_ = ConfigStage::Sending;
    send_command(now_ms);
}

void Ag3335::send_command(uint32_t now_ms) { await(kCommands[next_command_], now_ms); }

void Ag3335::next_command(uint32_t now_ms) {
    next_command_++;
    attempts_ = 0;
    if (next_command_ < kCommandCount) {
        send_command(now_ms);
        return;
    }
    stage_ = sequence_refused_ ? ConfigStage::Degraded : ConfigStage::Ready;
}

// INFO: fc 11nov26 a refused command is skipped, so no later setting stays on its default
void Ag3335::retry_command(uint32_t now_ms) {
    if (attempts_ < kCommandAttempts) {
        send_command(now_ms);
        return;
    }
    refused_commands_++;
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
    if (heard != PairReader::Heard::Ack || reader_.ack_command() != awaited_id_) return;
    ack_heard_ = true;
    ack_result_ = reader_.ack_result();
}

bool Ag3335::poll(uint32_t now_ms) {
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
