#include "hardware/parts/l76k/l76k.h"

namespace skyblip::parts {

const char* L76k::sentence_of(ports::GnssCommand command) {
    switch (command) {
        case ports::GnssCommand::Identify: return "$PCAS06,0*1B\r\n";
        case ports::GnssCommand::Constellations: return "$PCAS04,7*1E\r\n";
        case ports::GnssCommand::SentenceSet: return "$PCAS03,1,0,1,0,1,0,0,0,0,0,,,0,0*03\r\n";
        case ports::GnssCommand::DynamicModel: return "$PCAS11,6*1B\r\n";
        case ports::GnssCommand::Rate1Hz: return "$PCAS02,1000*2E\r\n";
        case ports::GnssCommand::Baud115200: return "$PCAS01,5*19\r\n";
        case ports::GnssCommand::Rate4Hz: return "$PCAS02,250*18\r\n";
        case ports::GnssCommand::Gsa4: return "$PCAS03,,,4,,,,,,,,,,,*36\r\n";
        case ports::GnssCommand::Rate2Hz: return "$PCAS02,500*1A\r\n";
        case ports::GnssCommand::Gsa2: return "$PCAS03,,,2,,,,,,,,,,,*30\r\n";
        // INFO: fc 18sep26 $PCAS03 takes a null per field meaning keep, so these move nGSV alone
        case ports::GnssCommand::GsvOff: return "$PCAS03,,,,0,,,,,,,,,,*32\r\n";
        case ports::GnssCommand::GsvEvery: return "$PCAS03,,,,1,,,,,,,,,,*33\r\n";
        case ports::GnssCommand::GsvPaced5: return "$PCAS03,,,,5,,,,,,,,,,*37\r\n";
        case ports::GnssCommand::GsvPaced9: return "$PCAS03,,,,9,,,,,,,,,,*3B\r\n";
        case ports::GnssCommand::RestartHot: return "$PCAS10,0*1C\r\n";
        case ports::GnssCommand::RestartWarm: return "$PCAS10,1*1D\r\n";
        case ports::GnssCommand::RestartCold: return "$PCAS10,2*1E\r\n";
        case ports::GnssCommand::RestartFactory: return "$PCAS10,3*1F\r\n";
        case ports::GnssCommand::None:
        case ports::GnssCommand::Wake:
        case ports::GnssCommand::ResetPulse: break;
    }
    return "";
}

void L76k::enter(ConfigStage stage, uint32_t now_ms) {
    if (stage == stage_) return;
    stage_ = stage;
    entered_ms_ = now_ms;
    note(ports::GnssEventKind::Stage, now_ms);
}

void L76k::note(ports::GnssEventKind kind, uint32_t now_ms) {
    ports::GnssEvent& event = journal_[journal_next_];
    journal_next_ = (journal_next_ + 1) % kJournalEvents;
    if (journal_count_ < kJournalEvents) journal_count_++;
    event = ports::GnssEvent{};
    event.kind = kind;
    if (kind == ports::GnssEventKind::Command) event.command = bring_up_.command;
    if (kind == ports::GnssEventKind::Verdict) event.verdict = bring_up_.verdict;
    event.stage = stage_;
    event.at_ms = now_ms;
    event.baud = baud_rate();
    event.interval_ms = static_cast<uint16_t>(solution_period_ms());
    event.interval_seen_ms = interval_seen_ms_;
    event.stamp_ms = static_cast<int16_t>(last_stamp_ms_ < 0 ? -1 : last_stamp_ms_ % 1000);
    event.steps = saturated(window_steps());
    event.heard = saturated(window_sentences());
}

bool L76k::take_event(ports::GnssEvent& out) {
    if (journal_count_ == 0) return false;
    out = journal_[(journal_next_ - journal_count_ + kJournalEvents) % kJournalEvents];
    journal_count_--;
    return true;
}

ports::GnssBringUp L76k::bring_up() const {
    ports::GnssBringUp reported = bring_up_;
    reported.interval_ms = static_cast<uint16_t>(solution_period_ms());
    reported.interval_seen_ms = interval_seen_ms_;
    return reported;
}

void L76k::conclude(ports::GnssVerdict verdict, uint32_t now_ms) {
    bring_up_.verdict = verdict;
    bring_up_.verify_steps = saturated(window_steps());
    if (cadence_ == Cadence::Quarter) {
        bring_up_.quarter_verdict = verdict;
        bring_up_.quarter_steps = bring_up_.verify_steps;
    }
    note(ports::GnssEventKind::Verdict, now_ms);
}

ports::GnssVerdict L76k::failure() const {
    if (window_sentences() < kMinVerifySentences) return ports::GnssVerdict::Silent;
    if (!obeying()) return ports::GnssVerdict::Disobeying;
    return ports::GnssVerdict::OffCadence;
}

bool L76k::silent(uint32_t now_ms) const {
    return now_ms - heard_ms_ >= kSilenceMs && now_ms - entered_ms_ >= kSilenceMs;
}

// INFO: fc 18nov26 a silent part is listened for at every rate each kProbePeriodMs, never given up
void L76k::start_probing(uint32_t now_ms) {
    bring_up_.probes++;
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
    send(ports::GnssCommand::Wake, now_ms);
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
    bring_up_.resets++;
    reset_released_ = false;
    reset_->mode_output(reset_pin_);
    reset_->set(reset_pin_, false);
    send(ports::GnssCommand::ResetPulse, now_ms);
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
