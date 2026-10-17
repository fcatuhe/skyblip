// core/annunciation/pattern.h: what the buzzer should be doing right now.
#ifndef SKYBLIP_CORE_ANNUNCIATION_PATTERN_H
#define SKYBLIP_CORE_ANNUNCIATION_PATTERN_H

#include <algorithm>
#include <cstdint>

#include "core/traffic/alarm.h"

namespace skyblip::annunciation {

// A piezo reaches full amplitude in a couple of milliseconds, so this is an ear
// figure, not a driver one: shorter than this and a blip is a click that a
// pilot cannot place, and cannot count.
constexpr uint16_t kShortestBlipEarCanPlaceMs = 90;

struct Note {
    uint16_t hz;
    uint16_t tone_ms;
    uint16_t gap_ms;
};

struct Tune {
    const Note* notes;
    uint8_t count;
};

template <uint8_t N>
constexpr Tune tune_of(const Note (&notes)[N]) {
    return Tune{notes, N};
}

constexpr uint16_t tune_ms(Tune tune) {
    uint16_t ms = 0;
    for (uint8_t i = 0; i < tune.count; i++)
        ms = static_cast<uint16_t>(ms + tune.notes[i].tone_ms + tune.notes[i].gap_ms);
    return ms;
}

// INFO: fc 17oct26 piezo unknown, 9x9 mm ones peak at 4 kHz +/- 500 Hz: the third straddles it
constexpr uint16_t kNoteA7Hz = 3520;
constexpr uint16_t kNoteCSharp8Hz = 4435;

constexpr uint16_t kAdvisorySixteenthMs = kShortestBlipEarCanPlaceMs;
constexpr uint16_t kAdvisorySkyMs = 2 * kAdvisorySixteenthMs;
constexpr uint16_t kAdvisoryBlipMs = kAdvisorySixteenthMs;
constexpr uint16_t kAdvisoryRestMs = 3 * kAdvisorySixteenthMs;
constexpr uint16_t kAdvisoryHeldBlipMs = 3 * kAdvisorySixteenthMs;

inline constexpr Note kAdvisoryCall[] = {
    {kNoteA7Hz, kAdvisorySkyMs, 0},
    {kNoteCSharp8Hz, kAdvisoryBlipMs, kAdvisoryRestMs},
    {kNoteA7Hz, kAdvisorySkyMs, 0},
    {kNoteCSharp8Hz, kAdvisoryHeldBlipMs, 0},
};
inline constexpr Tune kAdvisoryTune = tune_of(kAdvisoryCall);

// INFO: fc 12sep26 written C5 E5 G5, played two octaves up: a 4 kHz piezo whispers at 500 Hz
constexpr uint16_t kNoteC7Hz = 2093;
constexpr uint16_t kNoteE7Hz = 2637;
constexpr uint16_t kNoteG7Hz = 3136;

constexpr uint16_t kFirstFixNoteMs = kShortestBlipEarCanPlaceMs;
constexpr uint16_t kFirstFixBeatMs = 2 * kFirstFixNoteMs;
constexpr uint16_t kFirstFixNextBeatMs = kFirstFixBeatMs - kFirstFixNoteMs;
constexpr uint16_t kFirstFixSkipBeatMs = kFirstFixNextBeatMs + kFirstFixBeatMs;
constexpr uint16_t kFirstFixHeldNoteMs = kFirstFixBeatMs + kFirstFixNextBeatMs;

inline constexpr Note kFirstFixJingle[] = {
    {kNoteE7Hz, kFirstFixNoteMs, kFirstFixNextBeatMs},
    {kNoteE7Hz, kFirstFixNoteMs, kFirstFixSkipBeatMs},
    {kNoteE7Hz, kFirstFixNoteMs, kFirstFixSkipBeatMs},
    {kNoteC7Hz, kFirstFixNoteMs, kFirstFixNextBeatMs},
    {kNoteE7Hz, kFirstFixNoteMs, kFirstFixSkipBeatMs},
    {kNoteG7Hz, kFirstFixHeldNoteMs, 0},
};
inline constexpr Tune kFirstFixTune = tune_of(kFirstFixJingle);

constexpr uint16_t shortest_phase_ms(Tune tune) {
    uint16_t shortest = 0xFFFF;
    for (uint8_t i = 0; i < tune.count; i++) {
        shortest = std::min(tune.notes[i].tone_ms, shortest);
        if (tune.notes[i].gap_ms != 0) shortest = std::min(tune.notes[i].gap_ms, shortest);
    }
    return shortest;
}

// The shortest phase any tune asks the service loop to resolve. The loop
// cadence has to divide into this several times over or the tune the ear gets
// is not the tune written above; products assert that against their own step
// rate.
constexpr uint16_t kShortestPhaseMs =
    std::min(shortest_phase_ms(kAdvisoryTune), shortest_phase_ms(kFirstFixTune));
static_assert(kShortestPhaseMs >= kShortestBlipEarCanPlaceMs, "a phase the ear cannot place");

// Who is holding the buzzer. Traffic outranks the fix chirp, always: the rule
// is written here and nowhere else.
enum class Voice : uint8_t { None, FirstFix, Traffic };

Tune tune_for(Voice voice, traffic::Level level);

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
    // The level being announced, gaps included: a tune between two notes is
    // still announcing. 0 when the buzzer has been released.
    traffic::Level announcing_level() const {
        return voice_ == Voice::Traffic ? level_ : traffic::Level::None;
    }
    bool sounding() const { return commanded_on_; }
    uint8_t said() const { return said_; }

   private:
    void begin(Voice voice, traffic::Level level, uint32_t now_ms);
    void advance(uint32_t now_ms);
    void release();
    Command emit();

    Tune tune_{};
    Voice voice_{Voice::None};
    uint16_t tone_hz_{0};
    uint16_t commanded_hz_{0};
    traffic::Level level_{traffic::Level::None};
    uint8_t said_{0};
    uint8_t note_{0};
    uint32_t phase_ms_{0};
    bool phase_on_{false};
    bool commanded_on_{false};
};

}  // namespace skyblip::annunciation

#endif
