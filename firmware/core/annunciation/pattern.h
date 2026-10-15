// core/annunciation/pattern.h: what the buzzer should be doing right now.
// INFO: al 02aug26 the pair is SoftRF's fork's look cadence (oss/SoftRF-moshe-braner
// Buzzer.cpp:172-215)
#ifndef SKYBLIP_CORE_ANNUNCIATION_PATTERN_H
#define SKYBLIP_CORE_ANNUNCIATION_PATTERN_H

#include <cstdint>

#include "core/traffic/alarm.h"

namespace skyblip::annunciation {

// A piezo reaches full amplitude in a couple of milliseconds, so this is an ear
// figure, not a driver one: shorter than this and a blip is a click that a
// pilot cannot place, and cannot count.
constexpr uint16_t kShortestBlipEarCanPlaceMs = 90;

constexpr uint16_t kAdvisoryPairBeepMs = 250;
constexpr uint16_t kAdvisoryPairGapSameAsBeepMs = 250;
constexpr uint8_t kAdvisoryPairBeepCount = 2;

// INFO: fc 15oct26 piezo part unknown, 9x9 mm ones peak at 4 kHz +/- 500 Hz: the pair straddles it
constexpr uint16_t kAdvisoryWarbleLowHz = 3600;
constexpr uint16_t kAdvisoryWarbleHighHz = 4400;
constexpr uint16_t kAdvisoryWarblePeriodMs = kAdvisoryPairBeepMs;

constexpr uint16_t advisory_warble_hz(uint32_t into_beep_ms) {
    return (into_beep_ms % kAdvisoryWarblePeriodMs) < kAdvisoryWarblePeriodMs / 2
               ? kAdvisoryWarbleLowHz
               : kAdvisoryWarbleHighHz;
}

// INFO: fc 12sep26 written C5 E5 G5, played two octaves up: a 4 kHz piezo whispers at 500 Hz
constexpr uint16_t kNoteC7Hz = 2093;
constexpr uint16_t kNoteE7Hz = 2637;
constexpr uint16_t kNoteG7Hz = 3136;

constexpr uint16_t kFirstFixNoteMs = kShortestBlipEarCanPlaceMs;
constexpr uint16_t kFirstFixBeatMs = 2 * kFirstFixNoteMs;
constexpr uint16_t kFirstFixNextBeatMs = kFirstFixBeatMs - kFirstFixNoteMs;
constexpr uint16_t kFirstFixSkipBeatMs = kFirstFixNextBeatMs + kFirstFixBeatMs;
constexpr uint16_t kFirstFixHeldNoteMs = kFirstFixBeatMs + kFirstFixNextBeatMs;

struct Note {
    uint16_t hz;
    uint16_t tone_ms;
    uint16_t gap_ms;
};

constexpr Note kFirstFixJingle[] = {
    {kNoteE7Hz, kFirstFixNoteMs, kFirstFixNextBeatMs},
    {kNoteE7Hz, kFirstFixNoteMs, kFirstFixSkipBeatMs},
    {kNoteE7Hz, kFirstFixNoteMs, kFirstFixSkipBeatMs},
    {kNoteC7Hz, kFirstFixNoteMs, kFirstFixNextBeatMs},
    {kNoteE7Hz, kFirstFixNoteMs, kFirstFixSkipBeatMs},
    {kNoteG7Hz, kFirstFixHeldNoteMs, 0},
};
constexpr uint8_t kFirstFixNoteCount = sizeof(kFirstFixJingle) / sizeof(kFirstFixJingle[0]);

constexpr uint16_t first_fix_jingle_ms() {
    uint16_t ms = 0;
    for (const Note& note : kFirstFixJingle)
        ms = static_cast<uint16_t>(ms + note.tone_ms + note.gap_ms);
    return ms;
}

// The shortest phase any pattern asks the service loop to resolve. The loop
// cadence has to divide into this several times over or the pattern the ear
// gets is not the pattern written above; products assert that against their own
// step rate.
constexpr uint16_t kShortestPhaseMs = kShortestBlipEarCanPlaceMs;
static_assert(kShortestPhaseMs >= kShortestBlipEarCanPlaceMs, "a phase the ear cannot place");
static_assert(kAdvisoryWarblePeriodMs / 2 >= kShortestPhaseMs, "a pitch the loop cannot resolve");

struct Pattern {
    uint16_t tone_ms{0};
    uint16_t gap_ms{0};
    uint8_t repeats{0};
    // 0: said once, and not again until something changes.
    uint16_t reannounce_ms{0};
};

// Who is holding the buzzer. Traffic outranks the fix chirp, always: the rule
// is written here and nowhere else.
enum class Voice : uint8_t { None, FirstFix, Traffic };

Pattern pattern_for(Voice voice, traffic::Level level);

// Everything the decision depends on, gathered by the service that owns the
// annunciator and passed in whole.
struct Situation {
    // The level being announced right now, after the tracker's hysteresis.
    traffic::Level level{traffic::Level::None};
    bool announced{false};
    // The first fix landed on this pass.
    bool first_fix{false};
    // settings.alarm_enabled.
    bool enabled{true};
    // False from the moment the device starts going down.
    bool running{true};
};

// What the buzzer should be doing, and whether that differs from what it was
// already told. A PWM re-armed on every service pass is a click, not a tone, so
// the service acts on changed and on nothing else.
struct Command {
    bool tone_on{false};
    uint16_t tone_hz{0};
    bool changed{false};
};

class Policy {
   public:
    Command update(const Situation& situation, uint32_t now_ms);

    Voice voice() const { return voice_; }
    // The level being announced, gaps included: a pattern between two pulses is
    // still announcing. 0 when the buzzer has been released.
    traffic::Level announcing_level() const {
        return voice_ == Voice::Traffic ? level_ : traffic::Level::None;
    }
    bool sounding() const { return commanded_on_; }
    uint8_t said() const { return said_; }

   private:
    void begin(Voice voice, traffic::Level level, uint32_t now_ms);
    void advance(uint32_t now_ms);
    void play_pattern(uint32_t now_ms);
    void play_jingle(uint32_t now_ms);
    void release();
    Command emit();

    Pattern pattern_{};
    Voice voice_{Voice::None};
    uint16_t tone_hz_{0};
    uint16_t commanded_hz_{0};
    traffic::Level level_{traffic::Level::None};
    uint8_t said_{0};
    uint8_t note_{0};
    uint32_t announced_ms_{0};
    uint32_t phase_ms_{0};
    bool phase_on_{false};
    bool commanded_on_{false};
};

}  // namespace skyblip::annunciation

#endif
