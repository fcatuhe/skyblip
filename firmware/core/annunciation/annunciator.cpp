#include "core/annunciation/annunciator.h"

namespace skyblip::annunciation {

Tune tune_for(Voice voice, traffic::Level level) {
    switch (voice) {
        case Voice::None: return Tune{};
        case Voice::FirstFix: return kFirstFixTune;
        case Voice::Traffic:
            switch (level) {
                case traffic::Level::None: return Tune{};
                case traffic::Level::Advisory: return kAdvisoryTune;
            }
    }
    return Tune{};
}

Command Policy::update(const Situation& situation, uint32_t now_ms) {
    if (!situation.running || !situation.enabled) {
        release();
        return emit();
    }

    if (situation.level != traffic::Level::None) {
        if (voice_ != Voice::Traffic || level_ != situation.level || situation.announced)
            begin(Voice::Traffic, situation.level, now_ms);
    } else if (voice_ == Voice::Traffic) {
        release();
    }

    if (situation.first_fix && voice_ != Voice::Traffic)
        begin(Voice::FirstFix, traffic::Level::None, now_ms);

    advance(now_ms);
    return emit();
}

void Policy::begin(Voice voice, traffic::Level level, uint32_t now_ms) {
    voice_ = voice;
    level_ = level;
    tune_ = tune_for(voice, level);
    if (tune_.count == 0) {
        release();
        return;
    }
    phase_ms_ = now_ms;
    note_ = 0;
    tone_hz_ = tune_.notes[0].hz;
    phase_on_ = true;
    start_call();
}

void Policy::advance(uint32_t now_ms) {
    if (voice_ == Voice::None) return;
    while (true) {
        const Note& note = tune_.notes[note_];
        if (phase_on_) {
            if (now_ms - phase_ms_ < note.tone_ms) return;
            phase_ms_ += note.tone_ms;
            phase_on_ = false;
            continue;
        }
        if (note_ + 1 >= tune_.count) {
            if (voice_ == Voice::FirstFix) release();
            return;
        }
        if (now_ms - phase_ms_ < note.gap_ms) return;
        phase_ms_ += note.gap_ms;
        note_++;
        tone_hz_ = tune_.notes[note_].hz;
        phase_on_ = true;
        if (note.gap_ms != 0) start_call();
    }
}

void Policy::start_call() {
    if (felt(voice_)) vibrate_ms_ = call_ms();
}

uint16_t Policy::call_ms() const {
    uint16_t ms = 0;
    for (uint8_t i = note_; i < tune_.count; i++) {
        ms = static_cast<uint16_t>(ms + tune_.notes[i].tone_ms);
        if (tune_.notes[i].gap_ms != 0) break;
    }
    return ms;
}

void Policy::release() {
    voice_ = Voice::None;
    tone_hz_ = 0;
    level_ = traffic::Level::None;
    phase_on_ = false;
}

Command Policy::emit() {
    Command command{};
    command.tone_on = voice_ != Voice::None && phase_on_;
    command.tone_hz = command.tone_on ? tone_hz_ : 0;
    command.changed = command.tone_on != commanded_on_ || command.tone_hz != commanded_hz_;
    commanded_on_ = command.tone_on;
    commanded_hz_ = command.tone_hz;
    command.vibrate_ms = vibrate_ms_;
    vibrate_ms_ = 0;
    return command;
}

}  // namespace skyblip::annunciation
