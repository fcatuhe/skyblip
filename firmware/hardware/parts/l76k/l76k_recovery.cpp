#include "hardware/parts/l76k/l76k.h"

namespace skyblip::parts {

// INFO: fc 19nov26 a part that will not take what the product asked waits, then starts again
void L76k::recover(uint32_t now_ms) {
    recoveries_++;
    if (failed_rounds_ < kRecoveryBackoffSteps) failed_rounds_++;
    enter(ConfigStage::Degraded, now_ms);
}

uint32_t L76k::recovery_backoff_ms() const {
    return kRecoveryBackoffMs[failed_rounds_ == 0 ? 0 : failed_rounds_ - 1];
}

void L76k::retry_bring_up(uint32_t now_ms) {
    if (reset_ != nullptr) {
        begin_reset(now_ms);
        return;
    }
    attempts_ = 1;
    baud_tried_ = 1;
    begin_wake(now_ms);
}

bool L76k::silent(uint32_t now_ms) const {
    return now_ms - heard_ms_ >= kSilenceMs && now_ms - entered_ms_ >= kSilenceMs;
}

// INFO: fc 18nov26 a silent part is listened for at every rate each kProbePeriodMs, never given up
void L76k::start_probing(uint32_t now_ms) {
    probe_round_ms_ = now_ms;
    probe_bauds_ = 0;
    if (reset_ != nullptr && silent_rounds_ >= kSilentRoundsBeforeReset) {
        begin_reset(now_ms);
        return;
    }
    silent_rounds_++;
    enter(ConfigStage::Probing, now_ms);
    listen(now_ms);
}

void L76k::listen(uint32_t now_ms) {
    probe_bauds_++;
    flush();
    wake(now_ms);
    verify_sentences_ = parser_.solution().sentences;
}

void L76k::probe(uint32_t now_ms) {
    if (parser_.solution().sentences != verify_sentences_) {
        silent_rounds_ = 0;
        baud_tried_ = 1;
        attempts_ = 1;
        begin_wake(now_ms);
        return;
    }
    if (now_ms - last_command_ms_ < kProbeListenMs) return;
    if (probe_bauds_ < kBaudCandidateCount && step_baud()) {
        listen(now_ms);
        return;
    }
    if (now_ms - probe_round_ms_ >= kProbePeriodMs) start_probing(now_ms);
}

void L76k::begin_reset(uint32_t now_ms) {
    silent_rounds_ = 0;
    reset_released_ = false;
    reset_->mode_output(reset_pin_);
    reset_->set(reset_pin_, false);
    last_command_ms_ = now_ms;
    enter(ConfigStage::Resetting, now_ms);
}

// INFO: fc 18nov26 a reset part boots on its factory settings, so it is probed from 9600
void L76k::hold_reset(uint32_t now_ms) {
    if (!reset_released_) {
        if (now_ms - last_command_ms_ < kResetHoldMs) return;
        reset_->set(reset_pin_, true);
        reset_released_ = true;
        released_ms_ = now_ms;
        validity_.reset();
        adopt_baud(kFactoryBaudRate);
        return;
    }
    if (now_ms - released_ms_ >= kRestartSettleMs) start_probing(now_ms);
}

}  // namespace skyblip::parts
