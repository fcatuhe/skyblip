// What the device signs in the hand as it comes on and as it goes off, read pass by pass.
#include "core/indication/sign.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::indication;

namespace {

constexpr uint32_t kStepMs = 10;

struct Played {
    uint32_t lamp_pulses{0};
    uint32_t motor_pulses{0};
    uint32_t out_of_step_ms{0};
    Colour colour{Colour::None};
};

Played play(const Sign& sign, uint32_t from_ms) {
    SignPlayer player{};
    player.begin(sign, from_ms);
    Played played{};
    Signal previous{};
    for (uint32_t now_ms = from_ms; player.playing(); now_ms += kStepMs) {
        const Signal signal = player.update(now_ms);
        if (signal.lamp != Colour::None && previous.lamp == Colour::None) played.lamp_pulses++;
        if (signal.vibrating && !previous.vibrating) played.motor_pulses++;
        if ((signal.lamp != Colour::None) != signal.vibrating) played.out_of_step_ms += kStepMs;
        if (signal.lamp != Colour::None) played.colour = signal.lamp;
        previous = signal;
    }
    return played;
}

}  // namespace

TEST_CASE("sign: on is one green pulse, felt as it is lit") {
    const Played played = play(kSignOn, 0);
    CHECK(played.lamp_pulses == 1);
    CHECK(played.motor_pulses == 1);
    CHECK(played.colour == Colour::Green);
    CHECK(played.out_of_step_ms == 0);
}

TEST_CASE("sign: off is three red pulses, each felt as it is lit") {
    const Played played = play(kSignOff, 0);
    CHECK(played.lamp_pulses == 3);
    CHECK(played.motor_pulses == 3);
    CHECK(played.colour == Colour::Red);
    CHECK(played.out_of_step_ms == 0);
}

TEST_CASE("sign: it ends dark and still at its length, across the clock's wrap") {
    const uint32_t start_ms = 0xFFFFFFFFu - 200;
    SignPlayer player{};
    player.begin(kSignOff, start_ms);
    CHECK_FALSE(player.update(start_ms).vibrating);
    CHECK(player.update(start_ms + kSignOff.lead_ms).lamp == Colour::Red);
    CHECK(player.update(start_ms + kSignOff.lead_ms + kSignOffPulseMs).lamp == Colour::None);
    CHECK(player.update(start_ms + kSignOff.lead_ms + kSignOffPulseMs + kSignOffGapMs).vibrating);

    const Signal last = player.update(start_ms + length_ms(kSignOff));
    CHECK(last.lamp == Colour::None);
    CHECK_FALSE(last.vibrating);
    CHECK_FALSE(player.playing());
}

TEST_CASE("sign: a cancelled one stops at once") {
    SignPlayer player{};
    player.begin(kSignOn, 0);
    REQUIRE(player.update(0).vibrating);
    player.cancel();
    CHECK_FALSE(player.playing());
    CHECK_FALSE(player.update(kStepMs).vibrating);
}
