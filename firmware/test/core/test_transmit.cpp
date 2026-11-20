// When a burst goes out: the instant inside the direct slot, the channel it alternates to, and
// the rate G.1.16 sets for it.
#include <initializer_list>
#include <vector>

#include "core/flight/state.h"
#include "core/model/ownship.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "ports/rf.h"
#include "test/support/anchored_clock.h"
#include "test/support/fresh_fix.h"

using namespace skyblip;
using namespace skyblip::timing;

namespace {

Transmitter airborne_transmitter(uint32_t address = 0x5B7E57) {
    Transmitter transmitter;
    transmitter.configure(address);
    return transmitter;
}

// The plan the scheduler hands the radio service for a phase inside a slot.
SlotPlan slot_plan(int phase_ms) {
    Scheduler scheduler;
    return scheduler.plan(phase_ms, anchored());
}

int instant_at(uint32_t address, uint32_t utc_s) {
    const int phase = Transmitter::slot_in(utc_s) == 0 ? 500 : 900;
    return airborne_transmitter(address)
        .attempt(slot_plan(phase), utc_s, utc_s * 1000, true, kFixSolvedAtSecondEnd)
        .at_ms;
}

bool position_burst(const Transmitter::Attempt& attempt) {
    return attempt.go && attempt.payload == Transmitter::Payload::Position;
}

bool bursts_overlap(int one_ms, int other_ms) {
    const int apart = one_ms > other_ms ? one_ms - other_ms : other_ms - one_ms;
    return apart < static_cast<int>(Transmitter::kAirTimeMs);
}

}  // namespace

// A burst's first instant is its window's start, its last the window's end less air time and 5 ms.
TEST_CASE("transmit: a position on A goes out from 450 to 810 ms") {
    Transmitter transmitter = airborne_transmitter();
    int earliest = 1000, latest = 0;
    // Every other second is the lower channel's: §C.2.5's alternation is the clock's.
    for (uint32_t utc_s = 1000; utc_s < 6000; utc_s += 2) {
        const Transmitter::Attempt attempt =
            transmitter.attempt(slot_plan(500), utc_s, utc_s * 1000, true, kFixSolvedAtSecondEnd);
        REQUIRE(attempt.go);
        CHECK(attempt.frequency_hz == adsl::kMBand0Hz);
        if (attempt.at_ms < earliest) earliest = attempt.at_ms;
        if (attempt.at_ms > latest) latest = attempt.at_ms;
    }
    CHECK(earliest == 450);
    CHECK(latest == 810);
    CHECK(latest ==
          kWindowAMs.end_ms - static_cast<int>(Transmitter::kAirTimeMs) - kCompletionAllowanceMs);
}

TEST_CASE("transmit: a position on B goes out from 830 to 990 ms, where the direct slot ends") {
    Transmitter transmitter = airborne_transmitter();
    int earliest = 2000, latest = 0;
    for (uint32_t utc_s = 1; utc_s < 5000; utc_s += 2) {
        const Transmitter::Attempt attempt =
            transmitter.attempt(slot_plan(900), utc_s, utc_s * 1000, true, kFixSolvedAtSecondEnd);
        REQUIRE(attempt.go);
        if (attempt.at_ms < earliest) earliest = attempt.at_ms;
        if (attempt.at_ms > latest) latest = attempt.at_ms;
    }
    CHECK(earliest == 830);
    CHECK(latest == 990);
    CHECK(latest ==
          adsl::kDirectEndMs - static_cast<int>(Transmitter::kAirTimeMs) - kCompletionAllowanceMs);
}

TEST_CASE("transmit: each burst type's first and last instants are its window's") {
    using Payload = Transmitter::Payload;
    CHECK(Transmitter::window_of(Payload::Status, 0).start_ms == 450);
    CHECK(Transmitter::last_instant(Payload::Status, 0) == 810);
    CHECK(Transmitter::window_of(Payload::Status, 1).start_ms == 830);
    CHECK(Transmitter::last_instant(Payload::Status, 1) == 990);
    CHECK(Transmitter::window_of(Payload::Callsign, 1).start_ms == 1000);
    CHECK(Transmitter::last_instant(Payload::Callsign, 1) == 1190);
}

// The 2.2 ms channel check before a burst at a window's start falls in the switch's 3 ms lead.
TEST_CASE("transmit: the channel check before a window's first instant listens on its channel") {
    CHECK(PoliteAccess::kCcaLeadUs == 2200);
    CHECK(PoliteAccess::kCcaLeadUs <= static_cast<uint32_t>(kSwitchLeadMs) * 1000);
}

TEST_CASE("transmit: two devices do not pick the same instant every second") {
    Transmitter first = airborne_transmitter(0x5B7E57);
    Transmitter second = airborne_transmitter(0x123456);
    int same = 0;
    for (uint32_t utc_s = 0; utc_s < 100; utc_s += 2) {
        if (first.attempt(slot_plan(500), utc_s, utc_s * 1000, true, kFixSolvedAtSecondEnd).at_ms ==
            second.attempt(slot_plan(500), utc_s, utc_s * 1000, true, kFixSolvedAtSecondEnd).at_ms)
            same++;
    }
    CHECK(same < 5);
}

// Two addresses differ by a fixed XOR delta forever: a mixer that carried it would marry the pair.
TEST_CASE("transmit: a shared instant in one second is a fresh draw in the next") {
    constexpr uint32_t kOwnAddress = 0x5B7E57;
    constexpr uint32_t kPeerAddress = 0x5B003B;
    CHECK(instant_at(kOwnAddress, 1) == instant_at(kPeerAddress, 1));
    // 570 and 752, far more than a burst apart, from the pair that shared 850 ms a second earlier.
    CHECK(instant_at(kOwnAddress, 2) - instant_at(kPeerAddress, 2) == -182);

    const int shared_ms = instant_at(kOwnAddress, 1);
    const int own_next_ms = instant_at(kOwnAddress, 2);
    int shared = 0, still_overlapping = 0;
    for (uint32_t address = 0x5B0000; address < 0x5C0000; address++) {
        if (address == kOwnAddress || instant_at(address, 1) != shared_ms) continue;
        shared++;
        if (bursts_overlap(own_next_ms, instant_at(address, 2))) still_overlapping++;
    }
    REQUIRE(shared > 100);
    // About the odds of two 5 ms bursts meeting in a 361 ms slot. A carried delta keeps them all.
    CHECK(still_overlapping * 10 < shared);
}

// §C.2.5: traffic alternates between the two M-band channels.
// The second dwell hears traffic past 1000 ms. The direct slot ends there, so
// our own burst never does.
TEST_CASE("transmit: the burst completes inside the direct slot, tail or no tail") {
    Transmitter transmitter = airborne_transmitter();
    for (uint32_t utc_s = 1; utc_s < 400; utc_s += 2) {  // the odd seconds are the upper channel's
        const Transmitter::Attempt attempt =
            transmitter.attempt(slot_plan(900), utc_s, utc_s * 1000, true, kFixSolvedAtSecondEnd);
        REQUIRE(attempt.go);
        CHECK(attempt.frequency_hz == adsl::kMBand1Hz);
        CHECK(attempt.at_ms >= kWindowBMs.start_ms);
        CHECK(attempt.at_ms + static_cast<int>(Transmitter::kAirTimeMs) + kCompletionAllowanceMs <=
              adsl::kDirectEndMs);
    }
    // And the tail is not a position opportunity, however anchored the clock is.
    CHECK_FALSE(position_burst(
        transmitter.attempt(slot_plan(100), 500, 500000, true, kFixSolvedAtSecondEnd)));
}

TEST_CASE("transmit: consecutive transmissions alternate channel and slot") {
    Transmitter transmitter = airborne_transmitter();
    CHECK(
        transmitter.attempt(slot_plan(500), 10, 10000, true, kFixSolvedAtSecondEnd).frequency_hz ==
        adsl::kMBand0Hz);
    CHECK_FALSE(position_burst(
        transmitter.attempt(slot_plan(900), 10, 10000, true, kFixSolvedAtSecondEnd)));
    transmitter.sent(10, 10500);
    CHECK_FALSE(transmitter.attempt(slot_plan(500), 11, 11000, true, kFixSolvedAtSecondEnd).go);
    CHECK(
        transmitter.attempt(slot_plan(900), 11, 11000, true, kFixSolvedAtSecondEnd).frequency_hz ==
        adsl::kMBand1Hz);
}

// A burst refused for any reason used to shift the alternation for ever after.
TEST_CASE("transmit: a missed transmission does not put the channel out of step") {
    Transmitter transmitter = airborne_transmitter();
    CHECK(
        transmitter.attempt(slot_plan(500), 10, 10000, true, kFixSolvedAtSecondEnd).frequency_hz ==
        adsl::kMBand0Hz);
    CHECK(
        transmitter.attempt(slot_plan(500), 12, 12000, true, kFixSolvedAtSecondEnd).frequency_hz ==
        adsl::kMBand0Hz);
    CHECK(
        transmitter.attempt(slot_plan(900), 13, 13000, true, kFixSolvedAtSecondEnd).frequency_hz ==
        adsl::kMBand1Hz);
}

// §G.1.16, measured to the burst's own instant rather than to the top of its second.
TEST_CASE("transmit: a solution is aged at the burst's instant, not at the top of its second") {
    Transmitter transmitter = airborne_transmitter();
    const int at_ms =
        transmitter.attempt(slot_plan(500), 10, 10000, true, kFixSolvedAtSecondEnd).at_ms;
    const int32_t ceiling_ms = Transmitter::kFixMaxAgeMs - Transmitter::kFixAgeAllowanceMs;
    CHECK(transmitter.attempt(slot_plan(500), 10, 10000, true, ceiling_ms - at_ms).go);
    const Transmitter::Attempt late =
        transmitter.attempt(slot_plan(500), 10, 10000, true, ceiling_ms + 1 - at_ms);
    CHECK_FALSE(late.go);
    CHECK(late.stale);
    CHECK(late.at_ms == at_ms);

    // The frame ends inside the 500 ms: its air time and the bench's SetTx slack come off it.
    CHECK(ceiling_ms + static_cast<int32_t>(Transmitter::kAirTimeMs) + kCompletionAllowanceMs ==
          Transmitter::kFixMaxAgeMs);

    // A solution 250 ms before the instant, solved inside the second: a negative lag.
    CHECK(transmitter.attempt(slot_plan(500), 10, 10000, true, 250 - at_ms).go);

    // The top of the second's solution, however late in the slot the burst goes out, is not.
    CHECK_FALSE(transmitter.attempt(slot_plan(900), 11, 11000, true, 0).go);
}

TEST_CASE("transmit: nothing goes out unless the slot allows it") {
    Transmitter transmitter = airborne_transmitter();
    Scheduler scheduler;
    ClockState no_pps{true, false, 0};
    CHECK_FALSE(
        transmitter.attempt(scheduler.plan(500, no_pps), 10, 10000, true, kFixSolvedAtSecondEnd)
            .go);
    CHECK_FALSE(
        transmitter.attempt(scheduler.plan(300, anchored()), 10, 10000, true, kFixSolvedAtSecondEnd)
            .go);
    // Claimed from the retune at 442, but the instant is what waits for the window to open at 450.
    const Transmitter::Attempt early = transmitter.attempt(scheduler.plan(444, anchored()), 10,
                                                           10000, true, kFixSolvedAtSecondEnd);
    CHECK(early.go);
    CHECK(early.at_ms >= adsl::kDirectStartMs);
}

// C.5 on a held second: the bench's drift never carries a burst out of its window.
TEST_CASE("transmit: on a held second a burst keeps the drift away from its window's edges") {
    const ClockState held{true, false, kPpsHoldoverMs, 12'000'000};
    REQUIRE(holdover_drift_ms(held) == 10);
    int refused = 0;
    for (uint32_t utc_s = 1000; utc_s < 3000; utc_s++) {
        const int slot = Transmitter::slot_in(utc_s);
        const int phase = slot == 0 ? 500 : 900;
        const Transmitter::Attempt locked = airborne_transmitter().attempt(
            Scheduler::plan(phase, anchored()), utc_s, utc_s * 1000, true, kFixSolvedAtSecondEnd);
        const Transmitter::Attempt kept = airborne_transmitter().attempt(
            Scheduler::plan(phase, held), utc_s, utc_s * 1000, true, kFixSolvedAtSecondEnd);
        REQUIRE(locked.go);
        const int first = Transmitter::window_of(Transmitter::Payload::Position, slot).start_ms;
        const int last = Transmitter::last_instant(Transmitter::Payload::Position, slot);
        const bool clear = locked.at_ms >= first + 10 && locked.at_ms <= last - 10;
        CHECK(kept.go == clear);
        if (kept.go) CHECK(kept.at_ms == locked.at_ms);
        if (!clear) refused++;
    }
    CHECK(refused > 0);
}

// A transmitter at twice the design rate is faulted, and a faulted one that will not stop is worse.
TEST_CASE("transmit: an exhausted hour blocks the burst and says so") {
    Transmitter transmitter = airborne_transmitter();
    for (uint32_t i = 0; i < 7200; i++) transmitter.sent(i, i * 500);
    REQUIRE(transmitter.air_time().window_ms(3599500) == timing::AirTime::kBudgetMs);

    const Transmitter::Attempt attempt =
        transmitter.attempt(slot_plan(500), 7200, 3599500, true, kFixSolvedAtSecondEnd);
    CHECK_FALSE(attempt.go);
    CHECK(attempt.over_budget);
}

TEST_CASE("transmit: the design rate never reaches the limit, so nothing is ever blocked") {
    Transmitter transmitter = airborne_transmitter();
    for (uint32_t i = 0; i < 3600; i++) transmitter.sent(i, i * 1000);
    CHECK(transmitter.air_time().permille(3599000) == 5);
    const Transmitter::Attempt attempt =
        transmitter.attempt(slot_plan(500), 3600, 3600000, true, kFixSolvedAtSecondEnd);
    CHECK(attempt.go);
    CHECK_FALSE(attempt.over_budget);
}

// M. ports::Clock::milliseconds() wraps every 49.7 days and this device flies through
// that instant. The wrap is a value, not a wait: every case below sets the clock
// a few hundred milliseconds short of 0xFFFFFFFF and steps past it.
namespace {
constexpr uint32_t kBeforeWrap = 0xFFFFFC00u;  // 1024 ms short of the wrap
}

TEST_CASE("transmit: the ground schedule is UTC's, so the millisecond wrap cannot move it") {
    Transmitter transmitter = airborne_transmitter();
    const uint32_t owned = transmitter.cycle_start();
    const int phase = Transmitter::slot_in(owned) == 0 ? 500 : 900;
    transmitter.sent(owned - 20, kBeforeWrap);
    constexpr Transmitter::Payload kPosition = Transmitter::Payload::Position;
    CHECK(
        transmitter.attempt(slot_plan(phase), owned, 1024u, false, kFixSolvedAtSecondEnd, kPosition)
            .go);
    CHECK_FALSE(
        transmitter
            .attempt(slot_plan(phase), owned + 1, 2024u, false, kFixSolvedAtSecondEnd, kPosition)
            .go);
}
