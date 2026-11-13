// What the device signs in the hand as it comes on and as it goes off, read pass by pass.
#include "core/indication/signature.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::indication;

namespace {

constexpr uint32_t kStepMs = 10;

struct Played {
    uint32_t lamp_pulses{0};
    uint32_t motor_pulses{0};
    uint32_t out_of_step_ms{0};
    Lamp colour{Lamp::None};
};

Played play(const Signature& signature, uint32_t from_ms) {
    SignaturePlayer player{};
    player.begin(signature, from_ms);
    Played played{};
    Signal previous{};
    for (uint32_t now_ms = from_ms; player.playing(); now_ms += kStepMs) {
        const Signal signal = player.update(now_ms);
        if (signal.lamp != Lamp::None && previous.lamp == Lamp::None) played.lamp_pulses++;
        if (signal.vibrating && !previous.vibrating) played.motor_pulses++;
        if ((signal.lamp != Lamp::None) != signal.vibrating) played.out_of_step_ms += kStepMs;
        if (signal.lamp != Lamp::None) played.colour = signal.lamp;
        previous = signal;
    }
    return played;
}

}  // namespace

TEST_CASE("signature: on is one green pulse, felt as it is lit") {
    const Played played = play(kSignOn, 0);
    CHECK(played.lamp_pulses == 1);
    CHECK(played.motor_pulses == 1);
    CHECK(played.colour == Lamp::Green);
    CHECK(played.out_of_step_ms == 0);
}

TEST_CASE("signature: off is three red pulses, each felt as it is lit") {
    const Played played = play(kSignOff, 0);
    CHECK(played.lamp_pulses == 3);
    CHECK(played.motor_pulses == 3);
    CHECK(played.colour == Lamp::Red);
    CHECK(played.out_of_step_ms == 0);
}

TEST_CASE("signature: it ends dark and still at its length, across the clock's wrap") {
    const uint32_t start_ms = 0xFFFFFFFFu - 200;
    SignaturePlayer player{};
    player.begin(kSignOff, start_ms);
    CHECK_FALSE(player.update(start_ms).vibrating);
    CHECK(player.update(start_ms + kSignOff.lead_ms).lamp == Lamp::Red);
    CHECK(player.update(start_ms + kSignOff.lead_ms + kSignOffPulseMs).lamp == Lamp::None);
    CHECK(player.update(start_ms + kSignOff.lead_ms + kSignOffPulseMs + kSignOffGapMs).vibrating);

    const Signal last = player.update(start_ms + length_ms(kSignOff));
    CHECK(last.lamp == Lamp::None);
    CHECK_FALSE(last.vibrating);
    CHECK_FALSE(player.playing());
}

TEST_CASE("signature: a cancelled one stops at once") {
    SignaturePlayer player{};
    player.begin(kSignOn, 0);
    REQUIRE(player.update(0).vibrating);
    player.cancel();
    CHECK_FALSE(player.playing());
    CHECK_FALSE(player.update(kStepMs).vibrating);
}
