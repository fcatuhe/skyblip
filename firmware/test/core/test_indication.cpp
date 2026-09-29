// The state-to-indicator table on its own: given the cell, the fix and
// the alarm, what should the one lamp on this device be doing right now.
//
// Why it exists: e-paper holds its last image down, so an off device looks like a running one.
//
// Every row declares whose budget it spends, and a static_assert refuses a row that exceeds it.
//
// The first case prints the whole table. That is deliberate: this is the one
// place support can read what a colour means, and `make test` output is where it
// is legible without a C++ compiler.
#include <string>

#include "core/indication/lamp.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::indication;
using skyblip::traffic::Level;

namespace {

// The service loop's own step. The policy is only ever asked at this rate, so the
// tests ask it at this rate too: a wink that only comes out right when sampled at
// a millisecond is not a wink this device can show.
constexpr uint32_t kStepMs = 10;

// A situation that is true of every lit row at once. Peeling one field off it at a
// time is how the priority order gets asserted instead of restated.
Situation everything_at_once() {
    Situation s{};
    s.running = true;
    s.alarm_level = Level::Advisory;
    s.power_level = power::PowerLevel::Flat;
    s.fix_valid = false;
    return s;
}

// What a lamp would have done, from the commands the policy issued. It integrates
// the lit time because duty cycle is the whole power argument and a claim about
// duty that nothing measures is a comment.
struct LampRig {
    Policy policy{};
    indication::Lamp shown{indication::Lamp::None};
    uint32_t shows{0};
    uint32_t lit_ms{0};
    uint32_t total_ms{0};
    uint32_t flashes{0};

    void step(const Situation& situation, uint32_t now_ms) {
        const Command command = policy.update(situation, now_ms);
        if (shown != indication::Lamp::None) lit_ms += kStepMs;
        total_ms += kStepMs;
        if (!command.changed) return;
        shows++;
        if (command.lamp != indication::Lamp::None && shown == indication::Lamp::None) flashes++;
        shown = command.lamp;
    }

    void run(const Situation& situation, uint32_t from, uint32_t to) {
        for (uint32_t t = from; t <= to; t += kStepMs) step(situation, t);
    }

    uint32_t duty_permille() const { return total_ms == 0 ? 0 : 1000u * lit_ms / total_ms; }
};

}  // namespace

TEST_CASE("indication: the whole state-to-indicator table, in priority order") {
    for (int i = 0; i < kRowCount; i++) {
        const Row& row = kTable[i];
        MESSAGE(i, ": ", std::string(to_string(row.condition)), " -> ",
                std::string(to_string(row.indication.lamp)), " on=", row.indication.on_ms,
                "ms off=", row.indication.off_ms,
                "ms duty=", indication::duty_permille(row.indication),
                "/1000 : ", std::string(row.meaning));
        REQUIRE(row.meaning != nullptr);
        CHECK(row.meaning[0] != 0);
        // The accessors and the row have to agree, or support reads one thing and
        // the device does another.
        CHECK(indication_for(row.condition).lamp == row.indication.lamp);
        CHECK(std::string(meaning_of(row.condition)) == row.meaning);
    }
    CHECK(kRowCount == static_cast<int>(Condition::kCount));
}

TEST_CASE("indication: no row spends more of the pack than the budget it declares") {
    for (int i = 0; i < kRowCount; i++) {
        const Row& row = kTable[i];
        const uint16_t duty = indication::duty_permille(row.indication);
        CHECK(duty <= duty_ceiling_permille(row.budget));
        // Every lit row is on the pack's bill, and nothing on the pack is held solid.
        if (row.budget != Budget::Dark) CHECK(row.indication.off_ms > 0);
        // A flash the eye would miss is a lamp that costs and says nothing.
        if (row.indication.lamp != indication::Lamp::None)
            CHECK(row.indication.on_ms >= kShortestFlashEyeCanCatchMs);
    }
}

TEST_CASE("indication: traffic outranks everything the device has to say about itself") {
    Situation s = everything_at_once();
    CHECK(condition_for(s) == Condition::Alarm);
    CHECK(indication_for(Condition::Alarm).lamp == indication::Lamp::Red);

    // And an empty sky does not.
    s.alarm_level = Level::None;
    CHECK(condition_for(s) != Condition::Alarm);
    s.alarm_level = kAlarmTakesLamp;
    CHECK(condition_for(s) == Condition::Alarm);
}

TEST_CASE("indication: the priority order is the table, top row first") {
    Situation s = everything_at_once();

    // Off outranks every reason to be lit: after park() nothing runs to advance a
    // blink anyway, and a lamp left showing anything is a device that looks on.
    s.running = false;
    CHECK(condition_for(s) == Condition::Off);
    s.running = true;

    CHECK(condition_for(s) == Condition::Alarm);
    s.alarm_level = Level::None;

    CHECK(condition_for(s) == Condition::Critical);
    s.power_level = power::PowerLevel::Low;

    CHECK(condition_for(s) == Condition::Low);
    s.power_level = power::PowerLevel::Normal;

    CHECK(condition_for(s) == Condition::NoFix);
    s.fix_valid = true;
    CHECK(condition_for(s) == Condition::Alive);
}

TEST_CASE("indication: the cutoff monitor decides what low means, not a second threshold") {
    Situation s{};
    s.power_level = power::PowerLevel::Critical;
    CHECK(condition_for(s) == Condition::Critical);
    s.power_level = power::PowerLevel::Flat;
    CHECK(condition_for(s) == Condition::Critical);
    s.power_level = power::PowerLevel::Low;
    CHECK(condition_for(s) == Condition::Low);
    // A unit whose divider is unpopulated reads Unknown for ever. It is not a low
    // cell and must not blink like one: the whole point of reading the published
    // level rather than comparing millivolts again.
    s.power_level = power::PowerLevel::Unknown;
    CHECK(condition_for(s) == Condition::NoFix);
}

TEST_CASE("indication: alive is a wink, not an LED left on") {
    Situation s{};
    s.power_level = power::PowerLevel::Normal;
    s.fix_valid = true;

    LampRig lamp;
    lamp.run(s, 0, 30000);
    CHECK(lamp.policy.condition() == Condition::Alive);
    // Thirty seconds of a healthy device: ten winks, and the lamp is dark for
    // more than 97% of it.
    CHECK(lamp.duty_permille() <= kSteadyDutyCeilingPermille);
    CHECK(lamp.flashes >= 9);
    CHECK(lamp.flashes <= 11);
}

TEST_CASE("indication: no fix is the same wink in another colour") {
    Situation s{};
    s.power_level = power::PowerLevel::Normal;
    s.fix_valid = false;

    LampRig lamp;
    lamp.run(s, 0, 30000);
    CHECK(lamp.policy.condition() == Condition::NoFix);
    CHECK(lamp.duty_permille() <= kSteadyDutyCeilingPermille);
    CHECK(indication_for(Condition::NoFix).lamp == indication::Lamp::Blue);
    // Same rhythm, so a pilot reads the colour and not a count of flashes.
    CHECK(indication_for(Condition::NoFix).on_ms == indication_for(Condition::Alive).on_ms);
    CHECK(indication_for(Condition::NoFix).off_ms == indication_for(Condition::Alive).off_ms);
}

TEST_CASE("indication: a critical cell keeps SoftRF's blink rate at a tenth of its duty") {
    // SoftRF toggles the status LED every 300 ms below the low threshold
    // (src/driver/LED.cpp:204-219), which is a 600 ms period at half duty. We keep
    // the rate, because that is the rate a pilot has been taught to read as
    // trouble, and not the duty: solid-ish is the term this item exists to avoid.
    const Indication& low = indication_for(Condition::Critical);
    CHECK(low.on_ms + low.off_ms == 600);
    CHECK(indication::duty_permille(low) <= 100);

    Situation s{};
    s.power_level = power::PowerLevel::Critical;
    LampRig lamp;
    lamp.run(s, 0, 6000);
    CHECK(lamp.policy.condition() == Condition::Critical);
    CHECK(lamp.flashes >= 9);
    CHECK(lamp.duty_permille() <= kTransientDutyCeilingPermille);
}

TEST_CASE("indication: no row is held, so every lit row is paid for in winks") {
    for (int i = 0; i < kRowCount; i++)
        if (kTable[i].indication.lamp != indication::Lamp::None)
            CHECK(kTable[i].indication.off_ms > 0);

    Situation s{};
    s.alarm_level = Level::Advisory;
    LampRig lamp;
    lamp.run(s, 0, 5000);
    CHECK(lamp.policy.condition() == Condition::Alarm);
    // The one condition a pilot must not miss, and it still spends a quarter of the time lit.
    CHECK(lamp.duty_permille() <= kTransientDutyCeilingPermille);
}

TEST_CASE("indication: the lamp goes dark the moment the device starts going down") {
    Situation s{};
    s.alarm_level = Level::Advisory;
    LampRig lamp;
    lamp.step(s, 0);
    REQUIRE(lamp.shown == indication::Lamp::Red);

    s.running = false;
    lamp.step(s, 1010);
    CHECK(lamp.shown == indication::Lamp::None);
    CHECK(lamp.policy.condition() == Condition::Off);
    // And it stays dark: nothing about the cell or the sky brings it back while
    // the device is on its way down.
    s.power_level = power::PowerLevel::Critical;
    lamp.run(s, 1010, 5000);
    CHECK(lamp.shown == indication::Lamp::None);
}

TEST_CASE("indication: a change shows itself at once, not at the end of the cycle") {
    Situation s{};
    s.power_level = power::PowerLevel::Normal;
    s.fix_valid = true;
    LampRig lamp;
    lamp.run(s, 0, 1000);
    // Mid-cycle: the wink is long over and the lamp is dark for another 2 s.
    REQUIRE(lamp.shown == indication::Lamp::None);

    s.alarm_level = Level::Advisory;
    lamp.step(s, 1010);
    CHECK(lamp.shown == indication::Lamp::Red);
}

TEST_CASE("indication: the lamp is told only when the answer changes") {
    Situation s{};
    s.power_level = power::PowerLevel::Normal;
    s.fix_valid = true;
    LampRig lamp;
    lamp.run(s, 0, 30000);
    // Ten winks in thirty seconds is twenty commands, not three thousand.
    CHECK(lamp.shows <= 2 * (lamp.flashes + 1));
}

TEST_CASE("indication: the millisecond counter wrapping costs one flash, not a stuck lamp") {
    // ports::Clock::millis() is 32-bit and wraps at 49.7 days. Everything here is
    // unsigned subtraction, so the wrap restarts a wink; an absolute comparison
    // would have left the lamp in whichever phase it was in for ever.
    constexpr uint32_t kJustBeforeWrap = 0xFFFFF000u;
    constexpr uint32_t kMsAcrossTheWrap = 0x3000u;
    Situation s{};
    s.power_level = power::PowerLevel::Normal;
    s.fix_valid = true;

    LampRig lamp;
    uint32_t now_ms = kJustBeforeWrap;
    for (uint32_t elapsed = 0; elapsed < kMsAcrossTheWrap; elapsed += kStepMs, now_ms += kStepMs)
        lamp.step(s, now_ms);
    const uint32_t flashes_across_the_wrap = lamp.flashes;
    lamp.run(s, now_ms, now_ms + 30000);
    CHECK(lamp.flashes > flashes_across_the_wrap);
    CHECK(lamp.duty_permille() <= kSteadyDutyCeilingPermille);
}

TEST_CASE("indication: every condition has a name and a lamp that can be named") {
    for (int i = 0; i < kRowCount; i++) {
        CHECK(std::string(to_string(kTable[i].condition)) != "?");
        CHECK(std::string(to_string(kTable[i].indication.lamp)) != "?");
    }
    CHECK(std::string(to_string(indication::Lamp::None)) == "dark");
}
