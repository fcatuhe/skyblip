// When a burst goes out: the instant inside the direct slot, the channel it alternates to, and
// the rate G.1.16 sets for it.
#include <initializer_list>

#include "core/flight/state.h"
#include "core/model/ownship.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "test/support/anchored_clock.h"

using namespace skyblip::timing;

namespace {

Transmitter airborne_transmitter(uint32_t addr = 0x5B7E57) {
    Transmitter t;
    t.configure(addr);
    return t;
}

// The plan the scheduler hands the radio service for a phase inside a slot.
SlotPlan slot_plan(int phase_ms) {
    Scheduler s;
    return s.plan(phase_ms, anchored());
}

int instant_at(uint32_t addr, uint32_t utc) {
    const int phase = Transmitter::slot_in(utc, true) == 0 ? 500 : 900;
    return airborne_transmitter(addr).attempt(slot_plan(phase), utc, utc * 1000, true, 0).at_ms;
}

bool position_burst(const Transmitter::Attempt& a) {
    return a.go && a.payload == Transmitter::Payload::Position;
}

bool bursts_overlap(int one_ms, int other_ms) {
    const int apart = one_ms > other_ms ? one_ms - other_ms : other_ms - one_ms;
    return apart < static_cast<int>(Transmitter::kAirTimeMs);
}

}  // namespace

TEST_CASE("transmit: the instant is inside the direct slot, with room for the burst") {
    Transmitter t = airborne_transmitter();
    int earliest = 1000, latest = 0;
    // Every other second is the lower channel's: §C.2.5's alternation is the clock's.
    for (uint32_t utc = 1000; utc < 6000; utc += 2) {
        const Transmitter::Attempt a = t.attempt(slot_plan(500), utc, utc * 1000, true, 0);
        REQUIRE(a.go);
        // Not from kSlot0Start: the dwell opens at 400, the direct slot at 450.
        CHECK(a.at_ms >= kDirectStart);
        // The burst also has to end before the dwell closes for the channel hop, not just
        // before the direct slot ends at 1000.
        CHECK(a.at_ms + static_cast<int>(Transmitter::kAirTimeMs) +
                  Transmitter::kCompletionSlackMs <=
              Scheduler::dwell_end(0));
        CHECK(a.freq_hz == kMband0Hz);
        if (a.at_ms < earliest) earliest = a.at_ms;
        if (a.at_ms > latest) latest = a.at_ms;
    }
    // The whole slot is usable: an open dwell is a tuned dwell, so 450 itself is
    // a legal instant and no guard is owed at the front.
    CHECK(earliest == kDirectStart);
    CHECK(latest == Scheduler::dwell_end(0) - Transmitter::kCompletionSlackMs -
                        static_cast<int>(Transmitter::kAirTimeMs));
}

// The upper channel's dwell opens at 800 already tuned - the hop guard before it
// is what paid for the retune - so 800 is its first legal instant.
TEST_CASE("transmit: the first instant of a dwell is the moment it opens") {
    Transmitter t = airborne_transmitter();
    int earliest = 2000, latest = 0;
    for (uint32_t utc = 1; utc < 5000; utc += 2) {
        const Transmitter::Attempt a = t.attempt(slot_plan(900), utc, utc * 1000, true, 0);
        REQUIRE(a.go);
        if (a.at_ms < earliest) earliest = a.at_ms;
        if (a.at_ms > latest) latest = a.at_ms;
    }
    CHECK(earliest == kSlot1Start);
    CHECK(latest ==
          kDirectEnd - Transmitter::kCompletionSlackMs - static_cast<int>(Transmitter::kAirTimeMs));
}

TEST_CASE("transmit: two devices do not pick the same instant every second") {
    Transmitter a = airborne_transmitter(0x5B7E57);
    Transmitter b = airborne_transmitter(0x123456);
    int same = 0;
    for (uint32_t utc = 0; utc < 100; utc += 2) {
        if (a.attempt(slot_plan(500), utc, utc * 1000, true, 0).at_ms ==
            b.attempt(slot_plan(500), utc, utc * 1000, true, 0).at_ms)
            same++;
    }
    CHECK(same < 5);
}

// Two addresses differ by a fixed XOR delta forever: a mixer that carried it would marry the pair.
TEST_CASE("transmit: a shared instant in one second is a fresh draw in the next") {
    constexpr uint32_t kOwn = 0x5B7E57;
    constexpr uint32_t kPeer = 0x5B0155;
    CHECK(instant_at(kOwn, 1) == instant_at(kPeer, 1));
    // 788 and 605, 36 burst lengths apart, from the pair that shared 881 ms a second earlier.
    CHECK(instant_at(kOwn, 2) - instant_at(kPeer, 2) == 183);

    const int shared_ms = instant_at(kOwn, 1);
    const int own_next_ms = instant_at(kOwn, 2);
    int shared = 0, still_overlapping = 0;
    for (uint32_t addr = 0x5B0000; addr < 0x5C0000; addr++) {
        if (addr == kOwn || instant_at(addr, 1) != shared_ms) continue;
        shared++;
        if (bursts_overlap(own_next_ms, instant_at(addr, 2))) still_overlapping++;
    }
    REQUIRE(shared > 100);
    // 4 of 333, the odds of two 5 ms bursts meeting in a 340 ms slot. A carried delta keeps 333.
    CHECK(still_overlapping * 10 < shared);
}

// §C.2.5: traffic alternates between the two M-band channels.
// The second dwell hears traffic past 1000 ms. The direct slot ends there, so
// our own burst never does.
TEST_CASE("transmit: the burst completes inside the direct slot, tail or no tail") {
    Transmitter t = airborne_transmitter();
    for (uint32_t utc = 1; utc < 400; utc += 2) {  // the odd seconds are the upper channel's
        const Transmitter::Attempt a = t.attempt(slot_plan(900), utc, utc * 1000, true, 0);
        REQUIRE(a.go);
        CHECK(a.freq_hz == kMband1Hz);
        CHECK(a.at_ms >= kSlot1Start);
        CHECK(a.at_ms + static_cast<int>(Transmitter::kAirTimeMs) +
                  Transmitter::kCompletionSlackMs <=
              kDirectEnd);
    }
    // And the tail is not a position opportunity, however anchored the clock is.
    CHECK_FALSE(position_burst(t.attempt(slot_plan(100), 500, 500000, true, 0)));
}

TEST_CASE("transmit: consecutive transmissions alternate channel and slot") {
    Transmitter t = airborne_transmitter();
    CHECK(t.attempt(slot_plan(500), 10, 10000, true, 0).freq_hz == kMband0Hz);
    CHECK_FALSE(position_burst(t.attempt(slot_plan(900), 10, 10000, true, 0)));
    t.sent(10, 10500);
    CHECK_FALSE(t.attempt(slot_plan(500), 11, 11000, true, 0).go);
    CHECK(t.attempt(slot_plan(900), 11, 11000, true, 0).freq_hz == kMband1Hz);
}

// A burst refused for any reason used to shift the alternation for ever after.
TEST_CASE("transmit: a missed transmission does not put the channel out of step") {
    Transmitter t = airborne_transmitter();
    CHECK(t.attempt(slot_plan(500), 10, 10000, true, 0).freq_hz == kMband0Hz);
    CHECK(t.attempt(slot_plan(500), 12, 12000, true, 0).freq_hz == kMband0Hz);
    CHECK(t.attempt(slot_plan(900), 13, 13000, true, 0).freq_hz == kMband1Hz);
}

// §G.1.16: at least 1 Hz airborne, 0.1 Hz on the ground, one second of UTC's own ten.
TEST_CASE("transmit: one burst per second airborne, one per ten on the ground") {
    Transmitter t = airborne_transmitter();
    t.sent(10, 10500);
    CHECK_FALSE(position_burst(t.attempt(slot_plan(900), 10, 10800, true, 0)));
    CHECK(t.attempt(slot_plan(900), 11, 11000, true, 0).go);

    Transmitter g = airborne_transmitter();
    const uint32_t owned = g.ground_second();
    for (uint32_t utc = owned + 1; utc < owned + 10; utc++) {
        CAPTURE(utc);
        CHECK_FALSE(position_burst(g.attempt(slot_plan(500), utc, utc * 1000, false, 0)));
        CHECK_FALSE(position_burst(g.attempt(slot_plan(900), utc, utc * 1000, false, 0)));
    }
    const int phase = Transmitter::slot_in(owned + 10, false) == 0 ? 500 : 900;
    CHECK(g.attempt(slot_plan(phase), owned + 10, (owned + 10) * 1000, false, 0).go);
}

TEST_CASE("transmit: the callsign goes out in the ground second, once every ten") {
    constexpr Transmitter::Payload kName = Transmitter::Payload::Callsign;
    Transmitter t = airborne_transmitter();
    const uint32_t owned = t.ground_second();
    const SlotPlan tail = slot_plan(kSlot1Start);

    for (uint32_t utc = owned + 1; utc < owned + Transmitter::kCallsignPeriodS; utc++) {
        CAPTURE(utc);
        CHECK_FALSE(t.attempt(tail, utc, utc * 1000, true, 0, kName).go);
    }

    const uint32_t due = owned + Transmitter::kCallsignPeriodS;
    const Transmitter::Attempt a = t.attempt(tail, due, due * 1000, true, 0, kName);
    REQUIRE(a.go);
    CHECK(a.payload == kName);
    CHECK(a.freq_hz == kMband1Hz);
    CHECK(a.at_ms >= kCallsignStart);
    CHECK(a.at_ms + static_cast<int>(Transmitter::kAirTimeMs) <= kCallsignEnd);

    t.sent(due, due * 1000, kName);
    CHECK_FALSE(t.attempt(tail, due, due * 1000, true, 0, kName).go);
}

// One slot-1 dwell carries both bursts, so the position has to be off the air before the name keys.
TEST_CASE("transmit: a slot-1 dwell owes the position and then the name, never at once") {
    int both = 0;
    for (uint32_t addr = 0x5B0000; addr < 0x5B0400; addr++) {
        Transmitter t = airborne_transmitter(addr);
        const uint32_t due = t.ground_second() + Transmitter::kCallsignPeriodS;
        if (Transmitter::slot_in(due, false) != Transmitter::kCallsignSlot) continue;
        CAPTURE(addr);
        const SlotPlan slot1 = slot_plan(kSlot1Start);
        const Transmitter::Attempt position =
            t.attempt(slot1, due, due * 1000, false, 0, Transmitter::Payload::Position);
        const Transmitter::Attempt name =
            t.attempt(slot1, due, due * 1000, false, 0, Transmitter::Payload::Callsign);
        REQUIRE(position.go);
        REQUIRE(name.go);
        CHECK(position.freq_hz == name.freq_hz);
        CHECK(position.at_ms + static_cast<int>(Transmitter::kAirTimeMs) +
                  Transmitter::kCompletionSlackMs <=
              name.at_ms);
        both++;
    }
    CHECK(both > 400);
}

// Half of all addresses once lost every name to their own slot-1 position burst.
TEST_CASE("transmit: every address names itself every ten seconds, airborne or on the ground") {
    for (uint32_t addr = 0x5B0000; addr < 0x5B0400; addr++) {
        for (const bool airborne : {true, false}) {
            CAPTURE(addr);
            CAPTURE(airborne);
            Transmitter t = airborne_transmitter(addr);
            int named = 0;
            int positions = 0;
            for (uint32_t utc = 100; utc < 100 + 2 * Transmitter::kCallsignPeriodS; utc++) {
                for (const int phase : {500, kSlot1Start}) {
                    for (const Transmitter::Payload payload :
                         {Transmitter::Payload::Position, Transmitter::Payload::Callsign}) {
                        const Transmitter::Attempt a =
                            t.attempt(slot_plan(phase), utc, utc * 1000, airborne, 0, payload);
                        if (!a.go) continue;
                        t.sent(utc, utc * 1000 + static_cast<uint32_t>(phase), a.payload);
                        if (a.payload == Transmitter::Payload::Callsign)
                            named++;
                        else
                            positions++;
                    }
                }
            }
            CHECK(named == 2);
            // 20 s: one position a second airborne, two ground seconds on the ground.
            CHECK(positions == (airborne ? 20 : 2));
        }
    }
}

// The position burst is the one G.1.16 dates, and a name carries no instant to be stale.
TEST_CASE("transmit: a late solution refuses the position burst and not the callsign") {
    Transmitter t = airborne_transmitter();
    const uint32_t due = t.ground_second() + Transmitter::kCallsignPeriodS;
    const int32_t late = Transmitter::kFixLagMaxMs + 1;

    CHECK_FALSE(t.attempt(slot_plan(500), due, due * 1000, true, late).go);
    const Transmitter::Attempt a = t.attempt(slot_plan(kSlot1Start), due, due * 1000, true, late);
    REQUIRE(a.go);
    CHECK(a.payload == Transmitter::Payload::Callsign);
}

// Slot 0 is the other channel and the direct slot's own half of the second.
TEST_CASE("transmit: the callsign burst belongs to slot 1 and to no other dwell") {
    Transmitter t = airborne_transmitter();
    const uint32_t due = t.ground_second() + Transmitter::kCallsignPeriodS;
    t.sent(due, due * 1000);
    CHECK_FALSE(t.attempt(slot_plan(kSlot0Start), due, due * 1000, true, 0).go);
    CHECK(t.attempt(slot_plan(kSlot1Start), due, due * 1000, true, 0).go);
}

// A device that reboots comes back to the same second of the ten, not to a new one.
TEST_CASE("transmit: the ground second is the address's, not the boot's") {
    CHECK(airborne_transmitter(0x5B7E57).ground_second() ==
          airborne_transmitter(0x5B7E57).ground_second());

    int taken[skyblip::flight::kGroundReportPeriodS] = {0};
    for (uint32_t addr = 0x5B0000; addr < 0x5B1000; addr++)
        taken[airborne_transmitter(addr).ground_second()]++;
    for (uint32_t second = 0; second < skyblip::flight::kGroundReportPeriodS; second++) {
        CAPTURE(second);
        CHECK(taken[second] > 0x1000 / 20);
    }
}

// §G.1.16, measured to the top of the transmit second: what it refuses is a missed solution.
TEST_CASE("transmit: a missed solution is not transmitted, a late slot is") {
    Transmitter t = airborne_transmitter();
    CHECK(t.attempt(slot_plan(500), 10, 10000, true, 500).go);
    CHECK_FALSE(t.attempt(slot_plan(500), 10, 10000, true, 501).go);

    // This second's own solution, however late in the slot the burst goes out.
    CHECK(t.attempt(slot_plan(750), 10, 10750, true, 0).go);

    // A solution stamped after the second it opens: early, not stale.
    CHECK(t.attempt(slot_plan(500), 10, 10000, true, -20).go);
}

TEST_CASE("transmit: nothing goes out unless the slot allows it") {
    Transmitter t = airborne_transmitter();
    Scheduler s;
    ClockState no_pps{true, false, 0};
    CHECK_FALSE(t.attempt(s.plan(500, no_pps), 10, 10000, true, 0).go);
    CHECK_FALSE(t.attempt(s.plan(300, anchored()), 10, 10000, true, 0).go);
    // Claimed from 400, but the instant is what waits for the direct slot to open at 450.
    const Transmitter::Attempt early = t.attempt(s.plan(420, anchored()), 10, 10000, true, 0);
    CHECK(early.go);
    CHECK(early.at_ms >= kDirectStart);
}

TEST_CASE("transmit: own-ship goes on air only with a settled fix on an anchored clock") {
    skyblip::model::OwnState own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.tx_settled = true;
    CHECK(own_ship_transmits(own, anchored()));

    CHECK_FALSE(own_ship_transmits(own, ClockState{true, false, 0}));
    CHECK_FALSE(own_ship_transmits(own, ClockState{false, true, 0}));

    skyblip::model::OwnState unfixed = own;
    unfixed.fix_valid = false;
    CHECK_FALSE(own_ship_transmits(unfixed, anchored()));

    skyblip::model::OwnState untimed = own;
    untimed.utc_valid = false;
    CHECK_FALSE(own_ship_transmits(untimed, anchored()));

    skyblip::model::OwnState unsettled = own;
    unsettled.tx_settled = false;
    CHECK_FALSE(own_ship_transmits(unsettled, anchored()));
}

// A transmitter at twice the design rate is faulted, and a faulted one that will not stop is worse.
TEST_CASE("transmit: an exhausted hour blocks the burst and says so") {
    Transmitter t = airborne_transmitter();
    for (uint32_t i = 0; i < 7200; i++) t.sent(i, i * 500);
    REQUIRE(t.air_time().window_ms(3599500) == AirTime::kBudgetMs);

    const Transmitter::Attempt a = t.attempt(slot_plan(500), 7200, 3599500, true, 0);
    CHECK_FALSE(a.go);
    CHECK(a.over_budget);
}

TEST_CASE("transmit: the design rate never reaches the limit, so nothing is ever blocked") {
    Transmitter t = airborne_transmitter();
    for (uint32_t i = 0; i < 3600; i++) t.sent(i, i * 1000);
    CHECK(t.air_time().permille(3599000) == 5);
    const Transmitter::Attempt a = t.attempt(slot_plan(500), 3600, 3600000, true, 0);
    CHECK(a.go);
    CHECK_FALSE(a.over_budget);
}

// M. ports::Clock::millis() wraps every 49.7 days and this device flies through
// that instant. The wrap is a value, not a wait: every case below sets the clock
// a few hundred milliseconds short of 0xFFFFFFFF and steps past it.
namespace {
constexpr uint32_t kBeforeWrap = 0xFFFFFC00u;  // 1024 ms short of the wrap
}

TEST_CASE("transmit: the ground schedule is UTC's, so the millisecond wrap cannot move it") {
    Transmitter g = airborne_transmitter();
    const uint32_t owned = g.ground_second();
    const int phase = Transmitter::slot_in(owned, false) == 0 ? 500 : 900;
    g.sent(owned - 10, kBeforeWrap);
    CHECK(g.attempt(slot_plan(phase), owned, 1024u, false, 0).go);
    CHECK_FALSE(g.attempt(slot_plan(phase), owned + 1, 2024u, false, 0).go);
}
