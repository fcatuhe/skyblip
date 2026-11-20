// INFO: fc 18sep26 charge is not a row: this board's charger IC drives its own LED

#ifndef SKYBLIP_CORE_INDICATION_LAMP_H
#define SKYBLIP_CORE_INDICATION_LAMP_H

#include <algorithm>
#include <cstdint>

#include "core/power/cutoff.h"
#include "core/timing/slot.h"
#include "core/traffic/alarm.h"
#include "ports/indicator.h"

namespace skyblip::indication {

enum class Condition : uint8_t {
    Off,
    Alarm,
    Prompt,
    Pairing,
    Critical,
    Low,
    NoFix,
    Alive,
    NoFixLinked,
    AliveLinked,
    NoFixCapturing,
    AliveCapturing,
    Count
};

// INFO: fc 20nov26 an LED lights at once, so this is an eye figure: the shortest flash eyes catch
constexpr uint16_t kShortestFlashEyeCanCatchMs = 20;

struct Indication {
    Colour lamp{Colour::None};
    uint16_t on_ms{0};
    uint16_t off_ms{0};
    uint8_t flashes{1};
    uint16_t between_ms{0};
};

enum class Budget : uint8_t {
    Dark,
    Steady,
    Transient,
};

constexpr uint16_t kSteadyDutyCeilingPermille = 20;
constexpr uint16_t kTransientDutyCeilingPermille = 300;

struct Row {
    Condition condition;
    Indication indication;
    Budget budget;
};

constexpr int kRowCount = static_cast<int>(Condition::Count);

inline constexpr Row kTable[kRowCount] = {
    {Condition::Off, {Colour::None, 0, 0}, Budget::Dark},
    {Condition::Alarm, {Colour::Red, 50, 150}, Budget::Transient},
    {Condition::Prompt, {Colour::Green, 60, 190}, Budget::Transient},
    {Condition::Pairing, {Colour::Blue, 60, 190}, Budget::Transient},
    {Condition::Critical, {Colour::Red, 60, 540}, Budget::Transient},
    {Condition::Low, {Colour::Red, 30, 2970}, Budget::Steady},
    {Condition::NoFixCapturing, {Colour::Blue, 30, 2790, 2, 150}, Budget::Steady},
    {Condition::AliveCapturing, {Colour::Green, 30, 2790, 2, 150}, Budget::Steady},
    {Condition::NoFixLinked, {Colour::Blue, 30, 1470}, Budget::Steady},
    {Condition::AliveLinked, {Colour::Green, 30, 1470}, Budget::Steady},
    {Condition::NoFix, {Colour::Blue, 30, 2970}, Budget::Steady},
    {Condition::Alive, {Colour::Green, 30, 2970}, Budget::Steady},
};

constexpr uint32_t cycle_ms(const Indication& indication) {
    return static_cast<uint32_t>(indication.flashes) * indication.on_ms +
           static_cast<uint32_t>(indication.flashes - 1) * indication.between_ms +
           indication.off_ms;
}

constexpr uint16_t duty_permille(const Indication& indication) {
    const uint32_t cycle = cycle_ms(indication);
    if (indication.lamp == Colour::None || indication.on_ms == 0 || cycle == 0) return 0;
    return static_cast<uint16_t>(1000u * indication.flashes * indication.on_ms / cycle);
}

constexpr uint16_t duty_ceiling_permille(Budget budget) {
    switch (budget) {
        case Budget::Dark: return 0;
        case Budget::Steady: return kSteadyDutyCeilingPermille;
        case Budget::Transient:
        default: return kTransientDutyCeilingPermille;
    }
}

constexpr bool table_lists_each_condition_once() {
    for (int i = 0; i < kRowCount; i++)
        for (int j = i + 1; j < kRowCount; j++)
            if (kTable[i].condition == kTable[j].condition) return false;
    return true;
}

constexpr bool every_row_is_inside_its_budget() {
    for (int i = 0; i < kRowCount; i++)
        if (duty_permille(kTable[i].indication) > duty_ceiling_permille(kTable[i].budget))
            return false;
    return true;
}

constexpr bool every_lit_row_blinks() {
    for (int i = 0; i < kRowCount; i++)
        if (kTable[i].indication.lamp != Colour::None && kTable[i].indication.off_ms == 0)
            return false;
    return true;
}

constexpr uint16_t shortest_phase_ms() {
    uint16_t shortest = 0xFFFF;
    for (int i = 0; i < kRowCount; i++) {
        const Indication& indication = kTable[i].indication;
        if (indication.lamp == Colour::None) continue;
        shortest = std::min(indication.on_ms, shortest);
        shortest = std::min(indication.off_ms, shortest);
        if (indication.flashes > 1) shortest = std::min(indication.between_ms, shortest);
    }
    return shortest;
}

constexpr uint16_t kShortestPhaseMs = shortest_phase_ms();

static_assert(table_lists_each_condition_once(),
              "a condition was added to the enum and not to the table, or listed twice");
static_assert(every_row_is_inside_its_budget(),
              "a row spends more of the pack than the budget it declares");
static_assert(kTable[0].condition == Condition::Off,
              "a device on its way down outranks every reason to be lit");
static_assert(every_lit_row_blinks(), "a lamp held solid on an 850 mAh pack");
static_assert(kTable[1].condition == Condition::Alarm,
              "traffic outranks every other thing the device has to say");
static_assert(kShortestPhaseMs >= kShortestFlashEyeCanCatchMs, "a flash a glance would miss");

constexpr bool every_row_flashes_at_least_once() {
    for (int i = 0; i < kRowCount; i++)
        if (kTable[i].indication.flashes == 0) return false;
    return true;
}

static_assert(every_row_flashes_at_least_once(), "a row whose cycle holds no flash");

// INFO: fc 12nov26 every row's cycle divides the UTC minute, so a flash starts on second 00
constexpr uint32_t kUtcGridMs = 60000;

constexpr bool every_cycle_divides_the_minute() {
    for (int i = 0; i < kRowCount; i++) {
        const uint32_t cycle = cycle_ms(kTable[i].indication);
        if (cycle != 0 && kUtcGridMs % cycle != 0) return false;
    }
    return true;
}

static_assert(every_cycle_divides_the_minute(), "a row whose flashes drift off second 00");

const Indication& indication_for(Condition condition);
const char* to_string(Condition condition);
const char* to_string(Colour colour);
uint32_t until_edge_ms(const Indication& indication, uint32_t into_cycle_ms);

struct Situation {
    bool running{true};
    // INFO: fc 20nov26 the worst level standing, not the announced one, so buzzer gaps stay lit
    traffic::Level alarm_level{traffic::Level::None};
    // INFO: fc 20nov26 the cutoff monitor's level, so the lamp says low when the panel does
    power::PowerLevel power_level{power::PowerLevel::Unknown};
    bool fix_valid{false};
    // INFO: fc 12nov26 only a product without a screen sets these | 19nov26 through lamp_shows_link
    bool prompting{false};
    bool pairing{false};
    bool linked{false};
    // INFO: fc 15nov26 every product, since a page arms a capture on a Go too
    bool capturing{false};
};

constexpr traffic::Level kAlarmTakesLamp = traffic::Level::Advisory;

Condition condition_for(const Situation& situation);

struct Command {
    Colour lamp{Colour::None};
    bool changed{false};
};

class Policy {
   public:
    Command update(const Situation& situation, uint32_t now_ms, const timing::ClockState& clock);

    Condition condition() const { return condition_; }
    Colour lamp() const { return shown_; }
    bool on_utc_grid() const { return on_grid_; }
    // INFO: fc 19nov26 until the lamp next lights or darkens, from the phase the last update found
    uint32_t until_change_ms(uint32_t now_ms) const;

   private:
    uint32_t into_cycle_ms(const Indication& indication, uint32_t now_ms,
                           const timing::ClockState& clock);

    Condition condition_{Condition::Off};
    Colour shown_{Colour::None};
    uint32_t cycle_start_ms_{0};
    uint32_t into_ms_{0};
    uint32_t updated_ms_{0};
    bool on_grid_{false};
    bool started_{false};
};

}  // namespace skyblip::indication

#endif
