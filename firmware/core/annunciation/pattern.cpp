#include "core/annunciation/pattern.h"

namespace skyblip::annunciation {

Pattern pattern_for(Voice voice, traffic::Level level) {
    Pattern p{};
    if (voice != Voice::Traffic) return p;
    switch (level) {
        case traffic::Level::None: return p;
        case traffic::Level::Advisory:
            p.tone_ms = kAdvisoryPairBeepMs;
            p.gap_ms = kAdvisoryPairGapSameAsBeepMs;
            p.repeats = kAdvisoryPairBeepCount;
            return p;
    }
    return p;
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
    pattern_ = pattern_for(voice, level);
    announced_ms_ = now_ms;
    phase_ms_ = now_ms;
    note_ = 0;
    tone_hz_ = 0;
    if (voice == Voice::FirstFix) {
        tone_hz_ = kFirstFixJingle[0].hz;
        said_ = 1;
        phase_on_ = true;
        return;
    }
    said_ = pattern_.repeats > 0 ? 1 : 0;
    phase_on_ = pattern_.repeats > 0 && pattern_.tone_ms > 0;
    if (!phase_on_) release();
}

void Policy::play_jingle(uint32_t now_ms) {
    while (true) {
        const Note& note = kFirstFixJingle[note_];
        if (phase_on_) {
            if (now_ms - phase_ms_ < note.tone_ms) return;
            phase_ms_ += note.tone_ms;
            phase_on_ = false;
            continue;
        }
        if (note.gap_ms == 0 || note_ + 1 >= kFirstFixNoteCount) {
            release();
            return;
        }
        if (now_ms - phase_ms_ < note.gap_ms) return;
        phase_ms_ += note.gap_ms;
        note_++;
        said_++;
        tone_hz_ = kFirstFixJingle[note_].hz;
        phase_on_ = true;
    }
}

void Policy::advance(uint32_t now_ms) {
    if (voice_ == Voice::None) return;
    if (voice_ == Voice::FirstFix) {
        play_jingle(now_ms);
        return;
    }
    play_pattern(now_ms);
    tone_hz_ = phase_on_ ? advisory_warble_hz(now_ms - phase_ms_) : 0;
}

void Policy::play_pattern(uint32_t now_ms) {
    while (true) {
        if (phase_on_) {
            if (now_ms - phase_ms_ < pattern_.tone_ms) return;
            phase_ms_ += pattern_.tone_ms;
            phase_on_ = false;
            continue;
        }
        // No gap means nothing follows, whatever the count says: a pattern that
        // cannot end is the bug this file was written for.
        if (said_ >= pattern_.repeats || pattern_.gap_ms == 0) break;
        if (now_ms - phase_ms_ < pattern_.gap_ms) return;
        phase_ms_ += pattern_.gap_ms;
        phase_on_ = true;
        said_++;
    }

    if (pattern_.reannounce_ms == 0) return;
    if (now_ms - announced_ms_ >= pattern_.reannounce_ms) begin(voice_, level_, now_ms);
}

void Policy::release() {
    voice_ = Voice::None;
    tone_hz_ = 0;
    level_ = traffic::Level::None;
    said_ = 0;
    phase_on_ = false;
}

Command Policy::emit() {
    Command command{};
    command.tone_on = voice_ != Voice::None && phase_on_;
    command.tone_hz = command.tone_on ? tone_hz_ : 0;
    command.changed = command.tone_on != commanded_on_ || command.tone_hz != commanded_hz_;
    commanded_on_ = command.tone_on;
    commanded_hz_ = command.tone_hz;
    return command;
}

}  // namespace skyblip::annunciation
