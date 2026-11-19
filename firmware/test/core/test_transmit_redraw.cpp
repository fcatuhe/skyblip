// A position whose instant passed before its fix came is drawn again over the rest of its window.
#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "test/support/anchored_clock.h"

using namespace skyblip;
using namespace skyblip::timing;

namespace {

constexpr uint32_t kEvenSecond = 1000;
constexpr int32_t kWholeSecondFix = 0;
constexpr int32_t kHalfSecondFix = -500;
constexpr int32_t kWholeSecondInHandMs = 70;

SlotPlan window_a() {
    Scheduler scheduler;
    return scheduler.plan(500, anchored());
}

Transmitter::Attempt position(uint32_t address, int32_t fix_lag_ms, int32_t fix_in_hand_ms) {
    Transmitter transmitter;
    transmitter.configure(address);
    return transmitter.attempt(window_a(), kEvenSecond, kEvenSecond * 1000, true, fix_lag_ms,
                               Transmitter::Payload::Position, fix_in_hand_ms);
}

// An address whose position on A is drawn between first and last, the .000 fix in hand early.
uint32_t drawn_between(int first_ms, int last_ms, uint32_t from = 0x5B0000) {
    for (uint32_t address = from; address < from + 100000; address++) {
        const Transmitter::Attempt drawn = position(address, kWholeSecondFix, kWholeSecondInHandMs);
        if ((drawn.go || drawn.stale) && drawn.at_ms >= first_ms && drawn.at_ms <= last_ms)
            return address;
    }
    FAIL("no address draws an instant in that range");
    return from;
}

}  // namespace

// The pinned case: drawn at .520, held, and its .500 fix in hand at .590, after the instant.
TEST_CASE("transmit: a position drawn before its .500 fix came is drawn again and goes out") {
    const uint32_t address = drawn_between(510, 530);

    const Transmitter::Attempt redrawn = position(address, kHalfSecondFix, 590);

    REQUIRE(redrawn.go);
    CHECK(redrawn.redrawn);
    CHECK(redrawn.at_ms >= 590 + Transmitter::kRedrawLeadMs);
    CHECK(redrawn.at_ms <= Transmitter::last_instant(Transmitter::Payload::Position, 0));
    CHECK(Transmitter::fresh(kHalfSecondFix + redrawn.at_ms));
}

// Units on one receiver get the .500 fix together, so a fixed offset from it would bunch them.
TEST_CASE("transmit: redrawn instants spread over the rest of the window, address by address") {
    std::vector<int> instants;
    uint32_t address = 0x5B0000;
    while (instants.size() < 400) {
        address = drawn_between(500, 585, address) + 1;
        const Transmitter::Attempt redrawn = position(address - 1, kHalfSecondFix, 590);
        REQUIRE(redrawn.go);
        instants.push_back(redrawn.at_ms);
    }
    const int first = 590 + Transmitter::kRedrawLeadMs;
    const int last = Transmitter::last_instant(Transmitter::Payload::Position, 0);
    const int middle = (first + last) / 2;
    const auto early =
        std::count_if(instants.begin(), instants.end(), [&](int at_ms) { return at_ms < middle; });
    CHECK(*std::min_element(instants.begin(), instants.end()) < first + 20);
    CHECK(*std::max_element(instants.begin(), instants.end()) > last - 20);
    CHECK(early > 150);
    CHECK(early < 250);
}

// A whole second's fix that came late still flies only while it is at most 490 ms old.
TEST_CASE("transmit: a redrawn instant keeps the solution at most 490 ms old on air") {
    const uint32_t address = drawn_between(451, 460);

    const Transmitter::Attempt redrawn = position(address, kWholeSecondFix, 462);

    REQUIRE(redrawn.go);
    CHECK(redrawn.at_ms >= 462 + Transmitter::kRedrawLeadMs);
    CHECK(redrawn.at_ms <= Transmitter::kFixMaxAgeMs - Transmitter::kFixAgeAllowanceMs);
}

TEST_CASE("transmit: a .500 fix in hand by .810 still flies, one after it is too late on A") {
    const uint32_t address = drawn_between(500, 585);
    const int last = Transmitter::last_instant(Transmitter::Payload::Position, 0);
    CHECK(last == 810);

    // In hand at .810 itself, it keys after the channel check, inside the completion allowance.
    const Transmitter::Attempt latest = position(address, kHalfSecondFix, last);
    REQUIRE(latest.go);
    CHECK(latest.at_ms == last + Transmitter::kRedrawLeadMs);
    const Transmitter::Attempt lost = position(address, kHalfSecondFix, last + 1);
    CHECK_FALSE(lost.go);
    CHECK(lost.stale);
}

// A fix in hand before the drawn instant keeps it: the redraw is only for a fix that came late.
TEST_CASE("transmit: a fix in hand before the drawn instant flies at that instant") {
    const uint32_t address = drawn_between(600, 700);
    const int drawn = position(address, kWholeSecondFix, kWholeSecondInHandMs).at_ms;

    const Transmitter::Attempt kept = position(address, kHalfSecondFix, 590);

    REQUIRE(kept.go);
    CHECK_FALSE(kept.redrawn);
    CHECK(kept.at_ms == drawn);
}
