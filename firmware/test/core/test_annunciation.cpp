// The annunciation policy on its own: given a level, whether an aircraft was
// announced on this pass, and the time, what should the buzzer be doing now.
//
// The bug this file exists for: ports::Annunciator::alarm() opens a continuous
// tone that runs until silence(), and the service only silenced it when the
// level reached zero, so the buzzer sounded for as long as anything at all
// stayed inside the advisory window. Every case below is either a pattern that
// ends by itself or a release the service used to miss.
#include <initializer_list>

#include "core/annunciation/annunciator.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::annunciation;
using skyblip::traffic::Level;

namespace {

// The service loop's own step. The policy is only ever asked at this rate, so
// the tests ask it at this rate too: a pattern that only comes out right when
// sampled at a millisecond is not a pattern this device can play.
constexpr uint32_t kStepMs = 10;

// What a buzzer would have done, from the tone() and silence() commands the policy issued.
struct Buzzer {
    Policy policy{};
    bool on{false};
    uint16_t hz{0};
    int tone_commands{0};
    int pitch_steps{0};
    int silences{0};
    uint32_t on_ms{0};
    // The beep currently sounding, and the last one that finished.
    uint32_t beep_started_ms{0};
    uint32_t last_beep_ms{0};
    uint32_t last_gap_ms{0};
    uint32_t off_since_ms{0};
    int beeps{0};
    static constexpr int kRecorded = 16;
    uint16_t hz_of[kRecorded]{};
    uint16_t ended_on_hz[kRecorded]{};
    uint32_t beep_ms_of[kRecorded]{};
    uint32_t stepped_after_ms_of[kRecorded]{};
    bool vibrating{false};
    int vibrations{0};
    uint32_t vibrate_started_ms{0};
    uint32_t longest_beep_ms{0};
    uint32_t longest_vibration_ms{0};
    uint32_t vibrate_ms_of[kRecorded]{};
    uint32_t vibrated_at_ms[kRecorded]{};
    uint32_t gap_before[kRecorded]{};

    void apply(const Command& command, uint32_t now_ms) {
        if (on) on_ms += kStepMs;
        if (command.vibration_changed && command.vibrating) {
            vibrations++;
            vibrate_started_ms = now_ms;
            if (vibrations <= kRecorded) vibrated_at_ms[vibrations - 1] = now_ms;
        } else if (command.vibration_changed) {
            const uint32_t ms = now_ms - vibrate_started_ms;
            if (vibrations <= kRecorded) vibrate_ms_of[vibrations - 1] = ms;
            if (ms > longest_vibration_ms) longest_vibration_ms = ms;
        }
        vibrating = command.vibrating;
        if (!command.tone_changed) return;
        if (command.tone_on) {
            tone_commands++;
            if (!on) {
                beeps++;
                beep_started_ms = now_ms;
                last_gap_ms = now_ms - off_since_ms;
                if (beeps <= kRecorded) {
                    hz_of[beeps - 1] = command.tone_hz;
                    gap_before[beeps - 1] = last_gap_ms;
                }
            } else if (command.tone_hz != hz) {
                pitch_steps++;
                if (beeps <= kRecorded) stepped_after_ms_of[beeps - 1] = now_ms - beep_started_ms;
            }
            on = true;
            hz = command.tone_hz;
            if (beeps <= kRecorded) ended_on_hz[beeps - 1] = hz;
            return;
        }
        silences++;
        if (on) {
            last_beep_ms = now_ms - beep_started_ms;
            if (beeps <= kRecorded) beep_ms_of[beeps - 1] = last_beep_ms;
            if (last_beep_ms > longest_beep_ms) longest_beep_ms = last_beep_ms;
            off_since_ms = now_ms;
        }
        on = false;
        hz = 0;
    }

    // Hold one situation for a while, at the rate the product asks at.
    void run(Situation situation, uint32_t& t, uint32_t ms) {
        const uint32_t until = t + ms;
        for (; t < until; t += kStepMs) apply(policy.update(situation, t), t);
    }

    // The same, with the announced flag true on the first pass only: that is
    // exactly how the tracker reports one aircraft entering the window.
    void announce(Situation situation, uint32_t& t, uint32_t ms) {
        situation.announced = true;
        apply(policy.update(situation, t), t);
        t += kStepMs;
        situation.announced = false;
        run(situation, t, ms - kStepMs);
    }
};

int calls_in(Tune tune) {
    int calls = 1;
    for (uint8_t i = 0; i + 1 < tune.count; i++)
        if (tune.notes[i].gap_ms > 0) calls++;
    return calls;
}

uint32_t sounding_ms_of(Tune tune) {
    uint32_t ms = 0;
    for (uint8_t i = 0; i < tune.count; i++) ms += tune.notes[i].tone_ms;
    return ms;
}

const int kAdvisoryCalls = calls_in(kAdvisoryTune);
const uint32_t kLastCallMs = kAdvisorySkyMs + kAdvisoryLongBlipMs;

Situation standing(Level level) {
    Situation s{};
    s.level = level;
    return s;
}

}  // namespace

TEST_CASE("annunciation: the advisory is sky-blip twice with a rest between, and it ends") {
    CHECK(tune_for(Voice::Traffic, Level::Advisory).notes == kAdvisoryTune.notes);
    CHECK(kAdvisoryCalls == 2);

    Buzzer buzzer;
    uint32_t t = 1000;
    buzzer.announce(standing(Level::Advisory), t, 5000);

    CHECK(buzzer.beeps == kAdvisoryCalls);
    CHECK(buzzer.gap_before[1] == kAdvisoryRestMs);
    CHECK(buzzer.on_ms == sounding_ms_of(kAdvisoryTune));
    CHECK_FALSE(buzzer.on);
}

// A held tone is masked by a noise that a moving one is heard through.
TEST_CASE("annunciation: each advisory call rises a third across 4 kHz, the last blip long") {
    Buzzer buzzer;
    uint32_t t = 1000;
    buzzer.announce(standing(Level::Advisory), t, 5000);
    REQUIRE(buzzer.beeps == kAdvisoryCalls);

    for (int i = 0; i < kAdvisoryCalls; i++) {
        CHECK(buzzer.hz_of[i] == kNoteA7Hz);
        CHECK(buzzer.ended_on_hz[i] == kNoteCSharp8Hz);
    }
    CHECK(buzzer.pitch_steps == kAdvisoryCalls);
    CHECK(buzzer.stepped_after_ms_of[0] == kAdvisorySkyMs);
    CHECK(buzzer.stepped_after_ms_of[1] == kAdvisorySkyMs);
    CHECK(buzzer.beep_ms_of[0] == kAdvisorySkyMs + kAdvisoryBlipMs);
    CHECK(buzzer.beep_ms_of[1] == kLastCallMs);
    CHECK(kNoteA7Hz < 4000);
    CHECK(kNoteCSharp8Hz > 4000);
    // An equal-tempered major third is 2^(4/12) = 1.2599.
    CHECK(kNoteCSharp8Hz * 10000 / kNoteA7Hz == 12599);
}

// Under a headset the motor is what gets through, so it says the same calls the buzzer does.
TEST_CASE("annunciation: the motor is felt for each advisory call, as long as the call") {
    Buzzer buzzer;
    uint32_t t = 1000;
    buzzer.announce(standing(Level::Advisory), t, 5000);

    REQUIRE(buzzer.vibrations == kAdvisoryCalls);
    CHECK(buzzer.vibrated_at_ms[0] == 1000);
    CHECK(buzzer.vibrate_ms_of[0] == buzzer.beep_ms_of[0]);
    CHECK(buzzer.vibrate_ms_of[1] == buzzer.beep_ms_of[1]);
    CHECK(buzzer.vibrate_ms_of[0] == kAdvisorySkyMs + kAdvisoryBlipMs);
    CHECK(buzzer.vibrate_ms_of[1] == kLastCallMs);
    CHECK(buzzer.vibrated_at_ms[1] - buzzer.vibrated_at_ms[0] ==
          buzzer.beep_ms_of[0] + buzzer.gap_before[1]);
}

// An advisory can stand for the whole climb. It is said when the aircraft
// arrives and not again, which is what a pilot can live with in a gaggle.
TEST_CASE("annunciation: a standing advisory is said once, however long it stands") {
    Buzzer buzzer;
    uint32_t t = 1000;
    buzzer.announce(standing(Level::Advisory), t, 5000);
    REQUIRE(buzzer.beeps == kAdvisoryCalls);

    buzzer.run(standing(Level::Advisory), t, 60000);
    CHECK(buzzer.beeps == kAdvisoryCalls);
    CHECK(buzzer.vibrations == kAdvisoryCalls);
    CHECK(buzzer.on_ms == sounding_ms_of(kAdvisoryTune));
    CHECK(buzzer.policy.announcing_level() == Level::Advisory);
}

TEST_CASE("annunciation: the buzzer and the motor are released the moment their reason goes") {
    // Four ways for the reason to go, and the tone and the motor stop for every one of them.
    SUBCASE("the level falls to nothing: the target has gone") {
        Buzzer buzzer;
        uint32_t t = 1000;
        buzzer.announce(standing(Level::Advisory), t, 200);
        REQUIRE(buzzer.on);
        REQUIRE(buzzer.vibrating);
        buzzer.run(standing(Level::None), t, kStepMs);
        CHECK_FALSE(buzzer.on);
        CHECK_FALSE(buzzer.vibrating);
        CHECK(buzzer.silences == 1);  // the release, mid-beep
        CHECK(buzzer.policy.announcing_level() == Level::None);

        // And it stays gone: nothing re-announces an empty sky.
        const int commands = buzzer.tone_commands;
        buzzer.run(standing(Level::None), t, 10000);
        CHECK(buzzer.tone_commands == commands);
    }

    SUBCASE("alarms are switched off in settings, mid-pulse") {
        Buzzer buzzer;
        uint32_t t = 1000;
        buzzer.announce(standing(Level::Advisory), t, 40);
        REQUIRE(buzzer.on);

        Situation off = standing(Level::Advisory);
        off.enabled = false;
        buzzer.run(off, t, kStepMs);
        CHECK_FALSE(buzzer.on);
        CHECK_FALSE(buzzer.vibrating);

        // Silent and still for as long as it is switched off, threat or no threat.
        const int commands = buzzer.tone_commands;
        const int vibrations = buzzer.vibrations;
        buzzer.run(off, t, 10000);
        CHECK(buzzer.tone_commands == commands);
        CHECK(buzzer.vibrations == vibrations);

        // And it comes back, heard and felt, when the pilot turns it on again.
        buzzer.run(standing(Level::Advisory), t, 200);
        CHECK(buzzer.tone_commands > commands);
        CHECK(buzzer.vibrations > vibrations);
    }

    SUBCASE("the device is going down, mid-pulse") {
        Buzzer buzzer;
        uint32_t t = 1000;
        buzzer.announce(standing(Level::Advisory), t, 40);
        REQUIRE(buzzer.on);

        Situation down = standing(Level::Advisory);
        down.running = false;
        buzzer.run(down, t, kStepMs);
        CHECK_FALSE(buzzer.on);
        CHECK_FALSE(buzzer.vibrating);
        const int commands = buzzer.tone_commands;
        buzzer.run(down, t, 30000);
        CHECK(buzzer.tone_commands == commands);
    }

    SUBCASE("the fix tune ends on its own clock, with nothing to release it") {
        Buzzer buzzer;
        uint32_t t = 1000;
        Situation fix{};
        fix.first_fix = true;
        buzzer.apply(buzzer.policy.update(fix, t), t);
        t += kStepMs;
        buzzer.run(standing(Level::None), t, 5000);
        CHECK(buzzer.beeps == kFirstFixTune.count);
        CHECK(buzzer.last_beep_ms == kFirstFixHeldNoteMs);
        CHECK_FALSE(buzzer.on);
        CHECK(buzzer.policy.voice() == Voice::None);
    }
}

// The motor is reserved for traffic, so a pilot who feels it knows what it means without looking.
TEST_CASE("annunciation: the fix tune is heard and never felt") {
    Buzzer buzzer;
    uint32_t t = 1000;
    Situation fix{};
    fix.first_fix = true;
    buzzer.apply(buzzer.policy.update(fix, t), t);
    t += kStepMs;
    buzzer.run(Situation{}, t, 5000);

    REQUIRE(buzzer.beeps == kFirstFixTune.count);
    CHECK(buzzer.vibrations == 0);
}

TEST_CASE("annunciation: the first fix is six notes, three pitches, and an uneven rhythm") {
    Buzzer buzzer;
    uint32_t t = 1000;
    Situation fix{};
    fix.first_fix = true;
    buzzer.apply(buzzer.policy.update(fix, t), t);
    t += kStepMs;
    buzzer.run(Situation{}, t, 5000);

    REQUIRE(buzzer.beeps == kFirstFixTune.count);
    uint32_t tone_ms = 0;
    for (uint8_t i = 0; i < kFirstFixTune.count; i++) {
        CHECK(buzzer.hz_of[i] == kFirstFixJingle[i].pitch_hz);
        if (i > 0) CHECK(buzzer.gap_before[i] == kFirstFixJingle[i - 1].gap_ms);
        tone_ms += kFirstFixJingle[i].tone_ms;
    }
    CHECK(buzzer.on_ms == tone_ms);

    // E E . E . C E . G: the two silent beats are the tune.
    CHECK(buzzer.gap_before[1] == kFirstFixNextBeatMs);
    CHECK(buzzer.gap_before[2] == kFirstFixSkipBeatMs);
    CHECK(buzzer.hz_of[3] == kNoteC7Hz);
    CHECK(buzzer.hz_of[3] < buzzer.hz_of[0]);
    CHECK(buzzer.hz_of[5] == kNoteG7Hz);
}

TEST_CASE("annunciation: both tunes are over inside two seconds") {
    Buzzer advisory;
    uint32_t t = 1000;
    advisory.announce(standing(Level::Advisory), t, 5000);
    REQUIRE(advisory.beeps == kAdvisoryCalls);
    CHECK(advisory.off_since_ms - 1000 < 2000);

    Buzzer fix_tune;
    uint32_t u = 1000;
    Situation fix{};
    fix.first_fix = true;
    fix_tune.apply(fix_tune.policy.update(fix, u), u);
    u += kStepMs;
    fix_tune.run(Situation{}, u, 5000);
    REQUIRE(fix_tune.beeps == kFirstFixTune.count);
    CHECK(fix_tune.off_since_ms - 1000 < 2000);
}

TEST_CASE("annunciation: traffic owns the buzzer, the fix tune only borrows it") {
    // A chirp while traffic stands is refused: one owner at a time, and the
    // pilot's answer to "where is the traffic" is not a chirp.
    Buzzer standing_traffic;
    uint32_t t = 1000;
    standing_traffic.announce(standing(Level::Advisory), t, 100);
    Situation both = standing(Level::Advisory);
    both.first_fix = true;
    standing_traffic.apply(standing_traffic.policy.update(both, t), t);
    CHECK(standing_traffic.policy.voice() == Voice::Traffic);
    CHECK(standing_traffic.policy.announcing_level() == Level::Advisory);
    CHECK(standing_traffic.vibrations == 1);

    // The other way round: a chirp in progress loses the buzzer the instant
    // traffic wants it. The chirp is cut, a rest follows, and from there the
    // traffic tune owns the cadence.
    Buzzer chirping;
    uint32_t u = 1000;
    Situation fix{};
    fix.first_fix = true;
    chirping.apply(chirping.policy.update(fix, u), u);
    u += kStepMs;
    chirping.run(Situation{}, u, kFirstFixNoteMs - 2 * kStepMs);
    REQUIRE(chirping.on);
    REQUIRE(chirping.policy.voice() == Voice::FirstFix);

    chirping.announce(standing(Level::Advisory), u, 3000);
    CHECK(chirping.policy.voice() == Voice::Traffic);
    CHECK(chirping.policy.announcing_level() == Level::Advisory);
    CHECK(chirping.beeps == 1 + kAdvisoryCalls);
    CHECK(chirping.gap_before[1] == kReannounceRestMs);
    CHECK(chirping.hz_of[1] == kNoteA7Hz);
    CHECK(chirping.last_beep_ms == kLastCallMs);
    CHECK(chirping.vibrations == kAdvisoryCalls);
}

TEST_CASE("annunciation: a second aircraft is announced, though the level has not moved") {
    Buzzer buzzer;
    uint32_t t = 1000;
    buzzer.announce(standing(Level::Advisory), t, 2000);
    REQUIRE(buzzer.beeps == kAdvisoryCalls);

    // With one level, the level alone never changes: the tracker saying that an
    // aircraft was announced is the only thing that can speak for the second one.
    buzzer.announce(standing(Level::Advisory), t, 2000);
    CHECK(buzzer.beeps == 2 * kAdvisoryCalls);
    CHECK(buzzer.vibrations == 2 * kAdvisoryCalls);
}

TEST_CASE(
    "annunciation: an aircraft announced mid-call is a new call after a rest, heard and felt") {
    Buzzer buzzer;
    uint32_t t = 1000;
    buzzer.announce(standing(Level::Advisory), t, 100);
    REQUIRE(buzzer.on);
    REQUIRE(buzzer.vibrating);

    buzzer.announce(standing(Level::Advisory), t, 3000);
    CHECK(buzzer.beeps == 1 + kAdvisoryCalls);
    CHECK(buzzer.vibrations == 1 + kAdvisoryCalls);
    CHECK(buzzer.beep_ms_of[0] == 100);
    CHECK(buzzer.gap_before[1] == kReannounceRestMs);
    CHECK(buzzer.beep_ms_of[1] == kAdvisorySkyMs + kAdvisoryBlipMs);
}

// A gaggle can announce faster than a call ends, and the guard must not be what ends it.
TEST_CASE("annunciation: a stream of announcements never holds an output past the longest call") {
    Buzzer buzzer;
    uint32_t t = 1000;
    for (int i = 0; i < 20; i++) buzzer.announce(standing(Level::Advisory), t, 150);
    buzzer.run(standing(Level::Advisory), t, 2000);

    CHECK(buzzer.beeps > 1);
    CHECK_FALSE(buzzer.on);
    CHECK(buzzer.longest_beep_ms <= longest_call_ms(kAdvisoryTune));
    CHECK(buzzer.longest_vibration_ms <= longest_call_ms(kAdvisoryTune));
    CHECK(longest_call_ms(kAdvisoryTune) < kAnnunciatorOnCeilingMs);
}
