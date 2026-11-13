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

// §G.1.16: at least 1 Hz airborne, and on the ground 0.1 Hz on average, 9 then 11 s apart.
TEST_CASE("transmit: one burst per second airborne, two per twenty on the ground") {
    Transmitter transmitter = airborne_transmitter();
    transmitter.sent(10, 10500);
    CHECK_FALSE(position_burst(transmitter.attempt(slot_plan(900), 10, 10800, true, 0)));
    CHECK(position_burst(transmitter.attempt(slot_plan(900), 11, 11000, true, 0)));

    const Transmitter on_ground = airborne_transmitter();
    const uint32_t g = on_ground.ground_second();
    int positions = 0;
    for (uint32_t utc_s = 1000 + g; utc_s < 1000 + g + Transmitter::kCycleS; utc_s++) {
        for (const int phase : {500, kSlot1StartMs}) {
            if (!position_burst(on_ground.attempt(slot_plan(phase), utc_s, utc_s * 1000, false, 0,
                                                  Transmitter::Payload::Position)))
                continue;
            CAPTURE(utc_s);
            const uint32_t offset = utc_s - 1000 - g;
            CHECK((offset == 0 || offset == 9));
            positions++;
        }
    }
    CHECK(positions == 2);
}

namespace {

struct Burst {
    uint32_t utc_s;
    Transmitter::Payload payload;
    int slot;
    int at_ms;
};

std::vector<Burst> flown(const Transmitter& transmitter, uint32_t from_s, uint32_t seconds,
                         bool airborne) {
    std::vector<Burst> bursts;
    for (uint32_t utc_s = from_s; utc_s < from_s + seconds; utc_s++) {
        for (const int slot : {0, 1}) {
            for (const Transmitter::Payload payload :
                 {Transmitter::Payload::Position, Transmitter::Payload::Status,
                  Transmitter::Payload::Callsign}) {
                const Transmitter::Attempt attempt =
                    transmitter.attempt(slot_plan(slot == 0 ? 500 : kSlot1StartMs), utc_s,
                                        utc_s * 1000, airborne, 0, payload);
                if (attempt.go) bursts.push_back({utc_s, payload, slot, attempt.at_ms});
            }
        }
    }
    return bursts;
}

int offset_of(const Transmitter& transmitter, uint32_t utc_s) {
    return static_cast<int>((utc_s + Transmitter::kCycleS - transmitter.ground_second()) %
                            Transmitter::kCycleS);
}

}  // namespace

// The six ground bursts of a cycle, for every serial: an odd g mirrors the channels of an even one.
TEST_CASE("transmit: on the ground a cycle is six bursts at +0, +2, +7, +9, +13 and +16") {
    using P = Transmitter::Payload;
    const struct {
        int offset;
        P payload;
        int even_g_slot;
    } kCycle[] = {{0, P::Position, 0}, {2, P::Status, 1},  {7, P::Callsign, 0},
                  {9, P::Position, 1}, {13, P::Status, 0}, {16, P::Callsign, 1}};
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        CAPTURE(serial);
        const Transmitter transmitter = airborne_transmitter(serial);
        const uint32_t g = transmitter.ground_second();
        const std::vector<Burst> bursts = flown(transmitter, 1000 + g, Transmitter::kCycleS, false);
        REQUIRE(bursts.size() == 6);
        for (size_t i = 0; i < bursts.size(); i++) {
            CHECK(offset_of(transmitter, bursts[i].utc_s) == kCycle[i].offset);
            CHECK(bursts[i].payload == kCycle[i].payload);
            CHECK(bursts[i].slot ==
                  (g % 2 == 0 ? kCycle[i].even_g_slot : 1 - kCycle[i].even_g_slot));
        }
    }
}

// What a receiver parked on one channel sees of a parked unit: never more than 7 s between bursts.
TEST_CASE("transmit: on the ground each channel hears a burst 7, 6, 7 and 7, 7, 6 s apart") {
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        CAPTURE(serial);
        const Transmitter transmitter = airborne_transmitter(serial);
        const uint32_t g = transmitter.ground_second();
        const std::vector<Burst> bursts =
            flown(transmitter, 1000 + g, 2 * Transmitter::kCycleS, false);
        std::vector<uint32_t> gaps[2];
        uint32_t last[2] = {0, 0};
        for (const Burst& burst : bursts) {
            const int channel = g % 2 == 0 ? burst.slot : 1 - burst.slot;
            if (last[channel] != 0) gaps[channel].push_back(burst.utc_s - last[channel]);
            last[channel] = burst.utc_s;
        }
        CHECK(gaps[0] == std::vector<uint32_t>{7, 6, 7, 7, 6});
        CHECK(gaps[1] == std::vector<uint32_t>{7, 7, 6, 7, 7});
    }
}

// C.2.5 asks for it of Traffic and of Status, and the parity rule gives it to the name as well.
TEST_CASE("transmit: every payload alternates channel from one transmission to the next") {
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        for (const bool airborne : {true, false}) {
            CAPTURE(serial);
            CAPTURE(airborne);
            const Transmitter transmitter = airborne_transmitter(serial);
            int last_slot[Transmitter::kPayloads] = {-1, -1, -1};
            int sent[Transmitter::kPayloads] = {};
            for (const Burst& burst : flown(transmitter, 1000, 60, airborne)) {
                const int type = static_cast<int>(burst.payload);
                if (last_slot[type] >= 0) CHECK(burst.slot != last_slot[type]);
                last_slot[type] = burst.slot;
                sent[type]++;
            }
            CHECK(sent[0] == (airborne ? 60 : 6));
            CHECK(sent[1] == 6);
            CHECK(sent[2] == 6);
        }
    }
}

// A takeoff or a landing changes nothing about the extras: same seconds, same channels.
TEST_CASE("transmit: airborne each extra flies on the channel the second's position leaves") {
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        CAPTURE(serial);
        const Transmitter transmitter = airborne_transmitter(serial);
        const std::vector<Burst> air = flown(transmitter, 1000, 60, true);
        const std::vector<Burst> ground = flown(transmitter, 1000, 60, false);
        std::vector<Burst> air_extras, ground_extras;
        for (const Burst& burst : air) {
            if (burst.payload == Transmitter::Payload::Position) {
                CHECK(burst.slot == Transmitter::slot_in(burst.utc_s));
                continue;
            }
            CHECK(burst.slot == 1 - Transmitter::slot_in(burst.utc_s));
            air_extras.push_back(burst);
        }
        for (const Burst& burst : ground)
            if (burst.payload != Transmitter::Payload::Position) ground_extras.push_back(burst);
        REQUIRE(air_extras.size() == ground_extras.size());
        for (size_t i = 0; i < air_extras.size(); i++) {
            CHECK(air_extras[i].utc_s == ground_extras[i].utc_s);
            CHECK(air_extras[i].slot == ground_extras[i].slot);
            CHECK(air_extras[i].payload == ground_extras[i].payload);
        }
    }
}

// The executor holds two bursts a dwell, and the parity rule never needs more than one.
TEST_CASE("transmit: no dwell owes more than one burst, airborne or on the ground") {
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        for (const bool airborne : {true, false}) {
            CAPTURE(serial);
            CAPTURE(airborne);
            const std::vector<Burst> bursts =
                flown(airborne_transmitter(serial), 1000, 60, airborne);
            for (size_t i = 1; i < bursts.size(); i++)
                CHECK((bursts[i].utc_s != bursts[i - 1].utc_s ||
                       bursts[i].slot != bursts[i - 1].slot));
        }
    }
    CHECK(skyblip::ports::RfBursts::kMaxBursts >= 1);
}

// EN 300 220-1's off-time: two bursts of ours on one frequency are at least 100 ms apart.
TEST_CASE("transmit: two bursts of ours on one frequency are at least 100 ms apart") {
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        for (const bool airborne : {true, false}) {
            CAPTURE(serial);
            CAPTURE(airborne);
            const std::vector<Burst> bursts =
                flown(airborne_transmitter(serial), 1000, 60, airborne);
            int64_t last_end_ms[2] = {-1000000, -1000000};
            for (const Burst& burst : bursts) {
                const int64_t start_ms = int64_t{burst.utc_s} * 1000 + burst.at_ms;
                CHECK(start_ms - last_end_ms[burst.slot] >= 100);
                last_end_ms[burst.slot] = start_ms + static_cast<int64_t>(Transmitter::kAirTimeMs);
            }
        }
    }
}

// G.1.16 dates the position, and neither a Status nor a name carries an instant to be stale.
TEST_CASE("transmit: a late solution refuses the position burst and not the extras") {
    const Transmitter transmitter = airborne_transmitter(1);
    const int32_t late = Transmitter::kFixMaxAgeMs + 1;
    const uint32_t name_s = 1000 + transmitter.ground_second() + Transmitter::kCallsignOffsetsS[0];
    REQUIRE(transmitter.callsign_second(name_s));
    CHECK_FALSE(transmitter
                    .attempt(slot_plan(Transmitter::slot_in(name_s) == 0 ? 500 : kSlot1StartMs),
                             name_s, name_s * 1000, true, late)
                    .go);
    const Transmitter::Attempt attempt = transmitter.attempt(
        slot_plan(Transmitter::extra_slot_in(name_s) == 0 ? 500 : kSlot1StartMs), name_s,
        name_s * 1000, true, late);
    REQUIRE(attempt.go);
    CHECK(attempt.payload == Transmitter::Payload::Callsign);
}

// C.5 reserves 0 to 200 ms for ground stations, and no burst of ours goes there any more.
TEST_CASE("transmit: slot 1's tail carries nothing of ours") {
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        CAPTURE(serial);
        const Transmitter transmitter = airborne_transmitter(serial);
        for (uint32_t utc_s = 1000; utc_s < 1020; utc_s++) {
            for (const Transmitter::Payload payload :
                 {Transmitter::Payload::Position, Transmitter::Payload::Status,
                  Transmitter::Payload::Callsign}) {
                const Transmitter::Attempt attempt =
                    transmitter.attempt(slot_plan(100), utc_s, utc_s * 1000, true, 0, payload);
                CHECK_FALSE(attempt.go);
            }
            for (const Burst& burst : flown(transmitter, utc_s, 1, true))
                CHECK(burst.at_ms + static_cast<int>(Transmitter::kAirTimeMs) <=
                      adsl::kDirectEndMs);
        }
    }
}

// A device that reboots comes back to the same second of the ten, not to a new one.
TEST_CASE("transmit: the ground second is the serial's last digit, whatever the prefix") {
    CHECK(airborne_transmitter(0x5B7E57).ground_second() == 0x7E57 % 10);
    CHECK(airborne_transmitter(0x007E57).ground_second() == 0x7E57 % 10);
    CHECK(airborne_transmitter(0x000001).ground_second() == 1);
}

// skyblip.eu hands out serials densely from 1, so a fleet fills the ten seconds evenly.
TEST_CASE("transmit: ten consecutive serials own ten different ground seconds") {
    for (uint32_t first = 1; first < 1000; first += 7) {
        CAPTURE(first);
        bool taken[skyblip::adsl::kGroundReportPeriodS] = {};
        for (uint32_t serial = first; serial < first + 10; serial++)
            taken[airborne_transmitter(serial).ground_second()] = true;
        for (const bool second : taken) CHECK(second);
    }
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
    const uint32_t owned = transmitter.ground_second();
    const int phase = Transmitter::slot_in(owned) == 0 ? 500 : 900;
    transmitter.sent(owned - 20, kBeforeWrap);
    CHECK(transmitter.attempt(slot_plan(phase), owned, 1024u, false, 0).go);
    CHECK_FALSE(transmitter.attempt(slot_plan(phase), owned + 20 + 1, 2024u, false, 0).go);
}
