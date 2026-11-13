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
        .attempt(slot_plan(phase), utc_s, utc_s * 1000, true, 0)
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

TEST_CASE("transmit: the instant is inside the direct slot, with room for the burst") {
    Transmitter transmitter = airborne_transmitter();
    int earliest = 1000, latest = 0;
    // Every other second is the lower channel's: §C.2.5's alternation is the clock's.
    for (uint32_t utc_s = 1000; utc_s < 6000; utc_s += 2) {
        const Transmitter::Attempt attempt =
            transmitter.attempt(slot_plan(500), utc_s, utc_s * 1000, true, 0);
        REQUIRE(attempt.go);
        // Not from kSlot0StartMs: the dwell opens at 400, the direct slot at 450.
        CHECK(attempt.at_ms >= adsl::kDirectStartMs);
        // The burst also has to end before the dwell closes for the channel hop, not just
        // before the direct slot ends at 1000.
        CHECK(attempt.at_ms + static_cast<int>(Transmitter::kAirTimeMs) +
                  Transmitter::kCompletionGuardMs <=
              Scheduler::dwell_end(0));
        CHECK(attempt.frequency_hz == adsl::kMBand0Hz);
        if (attempt.at_ms < earliest) earliest = attempt.at_ms;
        if (attempt.at_ms > latest) latest = attempt.at_ms;
    }
    // The whole slot is usable: an open dwell is a tuned dwell, so 450 itself is
    // a legal instant and no guard is owed at the front.
    CHECK(earliest == adsl::kDirectStartMs);
    CHECK(latest == Scheduler::dwell_end(0) - Transmitter::kCompletionGuardMs -
                        static_cast<int>(Transmitter::kAirTimeMs));
}

// The upper channel's dwell opens at 800 already tuned - the hop guard before it
// is what paid for the retune - so 800 is its first legal instant.
TEST_CASE("transmit: the first instant of a dwell is the moment it opens") {
    Transmitter transmitter = airborne_transmitter();
    int earliest = 2000, latest = 0;
    for (uint32_t utc_s = 1; utc_s < 5000; utc_s += 2) {
        const Transmitter::Attempt attempt =
            transmitter.attempt(slot_plan(900), utc_s, utc_s * 1000, true, 0);
        REQUIRE(attempt.go);
        if (attempt.at_ms < earliest) earliest = attempt.at_ms;
        if (attempt.at_ms > latest) latest = attempt.at_ms;
    }
    CHECK(earliest == kSlot1StartMs);
    CHECK(latest == adsl::kDirectEndMs - Transmitter::kCompletionGuardMs -
                        static_cast<int>(Transmitter::kAirTimeMs));
}

TEST_CASE("transmit: two devices do not pick the same instant every second") {
    Transmitter first = airborne_transmitter(0x5B7E57);
    Transmitter second = airborne_transmitter(0x123456);
    int same = 0;
    for (uint32_t utc_s = 0; utc_s < 100; utc_s += 2) {
        if (first.attempt(slot_plan(500), utc_s, utc_s * 1000, true, 0).at_ms ==
            second.attempt(slot_plan(500), utc_s, utc_s * 1000, true, 0).at_ms)
            same++;
    }
    CHECK(same < 5);
}

// Two addresses differ by a fixed XOR delta forever: a mixer that carried it would marry the pair.
TEST_CASE("transmit: a shared instant in one second is a fresh draw in the next") {
    constexpr uint32_t kOwnAddress = 0x5B7E57;
    constexpr uint32_t kPeerAddress = 0x5B01A0;
    CHECK(instant_at(kOwnAddress, 1) == instant_at(kPeerAddress, 1));
    // 532 and 573, 8 burst lengths apart, from the pair that shared 977 ms a second earlier.
    CHECK(instant_at(kOwnAddress, 2) - instant_at(kPeerAddress, 2) == -41);

    const int shared_ms = instant_at(kOwnAddress, 1);
    const int own_next_ms = instant_at(kOwnAddress, 2);
    int shared = 0, still_overlapping = 0;
    for (uint32_t address = 0x5B0000; address < 0x5C0000; address++) {
        if (address == kOwnAddress || instant_at(address, 1) != shared_ms) continue;
        shared++;
        if (bursts_overlap(own_next_ms, instant_at(address, 2))) still_overlapping++;
    }
    REQUIRE(shared > 100);
    // 4 of 376, the odds of two 5 ms bursts meeting in a 342 ms slot. A carried delta keeps 376.
    CHECK(still_overlapping * 10 < shared);
}

// §C.2.5: traffic alternates between the two M-band channels.
// The second dwell hears traffic past 1000 ms. The direct slot ends there, so
// our own burst never does.
TEST_CASE("transmit: the burst completes inside the direct slot, tail or no tail") {
    Transmitter transmitter = airborne_transmitter();
    for (uint32_t utc_s = 1; utc_s < 400; utc_s += 2) {  // the odd seconds are the upper channel's
        const Transmitter::Attempt attempt =
            transmitter.attempt(slot_plan(900), utc_s, utc_s * 1000, true, 0);
        REQUIRE(attempt.go);
        CHECK(attempt.frequency_hz == adsl::kMBand1Hz);
        CHECK(attempt.at_ms >= kSlot1StartMs);
        CHECK(attempt.at_ms + static_cast<int>(Transmitter::kAirTimeMs) +
                  Transmitter::kCompletionGuardMs <=
              adsl::kDirectEndMs);
    }
    // And the tail is not a position opportunity, however anchored the clock is.
    CHECK_FALSE(position_burst(transmitter.attempt(slot_plan(100), 500, 500000, true, 0)));
}

TEST_CASE("transmit: consecutive transmissions alternate channel and slot") {
    Transmitter transmitter = airborne_transmitter();
    CHECK(transmitter.attempt(slot_plan(500), 10, 10000, true, 0).frequency_hz == adsl::kMBand0Hz);
    CHECK_FALSE(position_burst(transmitter.attempt(slot_plan(900), 10, 10000, true, 0)));
    transmitter.sent(10, 10500);
    CHECK_FALSE(transmitter.attempt(slot_plan(500), 11, 11000, true, 0).go);
    CHECK(transmitter.attempt(slot_plan(900), 11, 11000, true, 0).frequency_hz == adsl::kMBand1Hz);
}

// A burst refused for any reason used to shift the alternation for ever after.
TEST_CASE("transmit: a missed transmission does not put the channel out of step") {
    Transmitter transmitter = airborne_transmitter();
    CHECK(transmitter.attempt(slot_plan(500), 10, 10000, true, 0).frequency_hz == adsl::kMBand0Hz);
    CHECK(transmitter.attempt(slot_plan(500), 12, 12000, true, 0).frequency_hz == adsl::kMBand0Hz);
    CHECK(transmitter.attempt(slot_plan(900), 13, 13000, true, 0).frequency_hz == adsl::kMBand1Hz);
}

// §G.1.16, measured to the top of the transmit second: what it refuses is a missed solution.
TEST_CASE("transmit: a missed solution is not transmitted, a late slot is") {
    Transmitter transmitter = airborne_transmitter();
    CHECK(transmitter.attempt(slot_plan(500), 10, 10000, true, 500).go);
    CHECK_FALSE(transmitter.attempt(slot_plan(500), 10, 10000, true, 501).go);

    // This second's own solution, however late in the slot the burst goes out.
    CHECK(transmitter.attempt(slot_plan(750), 10, 10750, true, 0).go);

    // A solution stamped after the second it opens: early, not stale.
    CHECK(transmitter.attempt(slot_plan(500), 10, 10000, true, -20).go);
}

TEST_CASE("transmit: nothing goes out unless the slot allows it") {
    Transmitter transmitter = airborne_transmitter();
    Scheduler scheduler;
    ClockState no_pps{true, false, 0};
    CHECK_FALSE(transmitter.attempt(scheduler.plan(500, no_pps), 10, 10000, true, 0).go);
    CHECK_FALSE(transmitter.attempt(scheduler.plan(300, anchored()), 10, 10000, true, 0).go);
    // Claimed from 400, but the instant is what waits for the direct slot to open at 450.
    const Transmitter::Attempt early =
        transmitter.attempt(scheduler.plan(420, anchored()), 10, 10000, true, 0);
    CHECK(early.go);
    CHECK(early.at_ms >= adsl::kDirectStartMs);
}

TEST_CASE("transmit: own-ship goes on air only with a settled fix on an anchored clock") {
    const skyblip::settings::AirAddress address = 0x00012Au;
    skyblip::model::Ownship own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.tx_settled = true;
    CHECK(ownship_transmits(own, anchored(), address));

    CHECK_FALSE(ownship_transmits(own, ClockState{true, false, 0}, address));
    CHECK_FALSE(ownship_transmits(own, ClockState{false, true, 0}, address));

    skyblip::model::Ownship unfixed = own;
    unfixed.fix_valid = false;
    CHECK_FALSE(ownship_transmits(unfixed, anchored(), address));

    skyblip::model::Ownship untimed = own;
    untimed.utc_valid = false;
    CHECK_FALSE(ownship_transmits(untimed, anchored(), address));

    skyblip::model::Ownship unsettled = own;
    unsettled.tx_settled = false;
    CHECK_FALSE(ownship_transmits(unsettled, anchored(), address));
}

// Issue #126: a unit the server never gave a serial has nothing it may transmit under.
TEST_CASE("transmit: own-ship with every fact but an address does not go on air") {
    skyblip::model::Ownship own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.tx_settled = true;
    CHECK_FALSE(ownship_transmits(own, anchored(), std::nullopt));
}

// A transmitter at twice the design rate is faulted, and a faulted one that will not stop is worse.
TEST_CASE("transmit: an exhausted hour blocks the burst and says so") {
    Transmitter transmitter = airborne_transmitter();
    for (uint32_t i = 0; i < 7200; i++) transmitter.sent(i, i * 500);
    REQUIRE(transmitter.air_time().window_ms(3599500) == timing::AirTime::kBudgetMs);

    const Transmitter::Attempt attempt =
        transmitter.attempt(slot_plan(500), 7200, 3599500, true, 0);
    CHECK_FALSE(attempt.go);
    CHECK(attempt.over_budget);
}

TEST_CASE("transmit: the design rate never reaches the limit, so nothing is ever blocked") {
    Transmitter transmitter = airborne_transmitter();
    for (uint32_t i = 0; i < 3600; i++) transmitter.sent(i, i * 1000);
    CHECK(transmitter.air_time().permille(3599000) == 5);
    const Transmitter::Attempt attempt =
        transmitter.attempt(slot_plan(500), 3600, 3600000, true, 0);
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
    CHECK(transmitter.attempt(slot_plan(phase), owned, 1024u, false, 0, kPosition).go);
    CHECK_FALSE(transmitter.attempt(slot_plan(phase), owned + 1, 2024u, false, 0, kPosition).go);
}
