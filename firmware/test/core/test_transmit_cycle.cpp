// The 20 s cycle each unit owns: seconds, channels, the retry 2 s on, and per-channel spacing.
#include <functional>
#include <initializer_list>
#include <vector>

#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "ports/rf.h"
#include "test/support/anchored_clock.h"

using namespace skyblip;
using namespace skyblip::timing;

namespace {

using Payload = Transmitter::Payload;

Transmitter transmitter_for(uint32_t serial) {
    Transmitter transmitter;
    transmitter.configure(serial);
    return transmitter;
}

SlotPlan slot_plan(int slot) {
    Scheduler scheduler;
    return scheduler.plan(slot == 0 ? 500 : kSlot1StartMs, anchored());
}

struct Burst {
    uint32_t utc_s;
    Payload payload;
    int slot;
    int at_ms;
    int64_t on_air_ms() const { return int64_t{utc_s} * 1000 + at_ms; }
};

using Refusal = std::function<bool(uint32_t utc_s, Payload payload)>;
using Airborne = std::function<bool(uint32_t utc_s)>;

std::vector<Burst> flown(Transmitter& transmitter, uint32_t from_s, uint32_t seconds,
                         const Airborne& airborne, const Refusal& refused = nullptr) {
    std::vector<Burst> bursts;
    for (uint32_t utc_s = from_s; utc_s < from_s + seconds; utc_s++) {
        transmitter.give_up(utc_s, airborne(utc_s), true);
        for (const int slot : {0, 1}) {
            for (const Payload payload : {Payload::Position, Payload::Status, Payload::Callsign}) {
                const Transmitter::Attempt attempt = transmitter.attempt(
                    slot_plan(slot), utc_s, utc_s * 1000, airborne(utc_s), 0, payload);
                if (!attempt.go) continue;
                if (refused && refused(utc_s, payload)) continue;
                transmitter.sent(utc_s, utc_s * 1000 + static_cast<uint32_t>(attempt.at_ms),
                                 payload);
                bursts.push_back({utc_s, payload, slot, attempt.at_ms});
            }
        }
    }
    return bursts;
}

std::vector<Burst> flown(Transmitter& transmitter, uint32_t from_s, uint32_t seconds, bool airborne,
                         const Refusal& refused = nullptr) {
    return flown(transmitter, from_s, seconds, [airborne](uint32_t) { return airborne; }, refused);
}

void check_spacing(const std::vector<Burst>& bursts) {
    int64_t last_end_ms[2] = {-1000000, -1000000};
    for (size_t i = 0; i < bursts.size(); i++) {
        const Burst& burst = bursts[i];
        CAPTURE(burst.utc_s);
        if (i > 0) CHECK((burst.utc_s != bursts[i - 1].utc_s || burst.slot != bursts[i - 1].slot));
        CHECK(burst.on_air_ms() - last_end_ms[burst.slot] >= 100);
        last_end_ms[burst.slot] = burst.on_air_ms() + static_cast<int64_t>(Transmitter::kAirTimeMs);
    }
    CHECK(skyblip::ports::RfBursts::kMaxBursts >= 1);
}

std::vector<Burst> of_type(const std::vector<Burst>& bursts, Payload payload) {
    std::vector<Burst> out;
    for (const Burst& burst : bursts)
        if (burst.payload == payload) out.push_back(burst);
    return out;
}

constexpr uint32_t kMinuteStartS = 2000;

}  // namespace

// skyblip.eu hands out serials densely from 1, so ten consecutive serials start ten cycles 2 s
// apart.
TEST_CASE("transmit cycle: a unit's cycle starts on an even second, 2 * (serial % 10)") {
    for (uint32_t first = 1; first <= 1024; first += 10) {
        bool taken[Transmitter::kCycleS] = {};
        for (uint32_t serial = first; serial < first + 10; serial++) {
            const uint32_t start = transmitter_for(serial).cycle_start();
            CHECK(start % 2 == 0);
            CHECK(start == 2 * (serial % 10));
            taken[start] = true;
        }
        for (uint32_t second = 0; second < Transmitter::kCycleS; second += 2) CHECK(taken[second]);
    }
}

// A at 0 and 11, B at 2 and 9, and the names in the tails of 4 and 14, 1000-1193 ms on.
TEST_CASE("transmit cycle: on the ground six bursts go at +0, +2, +4, +9, +11 and +14") {
    const struct {
        uint32_t offset;
        Payload payload;
        int slot;
    } kCycle[] = {{0, Payload::Position, 0}, {2, Payload::Status, 1},  {4, Payload::Callsign, 1},
                  {9, Payload::Position, 1}, {11, Payload::Status, 0}, {14, Payload::Callsign, 1}};
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        CAPTURE(serial);
        Transmitter transmitter = transmitter_for(serial);
        const uint32_t c = kMinuteStartS + transmitter.cycle_start();
        const std::vector<Burst> bursts = flown(transmitter, c, Transmitter::kCycleS, false);
        REQUIRE(bursts.size() == 6);
        for (size_t i = 0; i < bursts.size(); i++) {
            CHECK(bursts[i].utc_s - c == kCycle[i].offset);
            CHECK(bursts[i].payload == kCycle[i].payload);
            CHECK(bursts[i].slot == kCycle[i].slot);
            if (bursts[i].payload == Payload::Callsign) {
                CHECK(bursts[i].at_ms >= kCallsignStartMs);
                CHECK(bursts[i].at_ms + static_cast<int>(Transmitter::kAirTimeMs) <=
                      kCallsignEndMs);
            } else {
                CHECK(Scheduler::in_direct_slot(bursts[i].at_ms));
            }
        }
    }
}

// On air, A carries 0 and 11 of each cycle, B 2, 5, 9 and 15: a name goes out in the next second.
TEST_CASE("transmit cycle: on the ground A hears 11 and 9 s apart, B 3, 4, 6 and 7 s apart") {
    const auto gaps = [](const std::vector<int64_t>& seconds) {
        std::vector<int64_t> out;
        for (size_t i = 1; i < seconds.size(); i++) out.push_back(seconds[i] - seconds[i - 1]);
        return out;
    };
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        CAPTURE(serial);
        Transmitter transmitter = transmitter_for(serial);
        const uint32_t c = kMinuteStartS + transmitter.cycle_start();
        std::vector<int64_t> on_air[2];
        std::vector<int64_t> both;
        for (const Burst& burst : flown(transmitter, c, 2 * Transmitter::kCycleS, false)) {
            on_air[burst.slot].push_back(burst.on_air_ms() / 1000);
            both.push_back(burst.on_air_ms() / 1000);
        }
        CHECK(gaps(on_air[0]) == std::vector<int64_t>{11, 9, 11});
        CHECK(gaps(on_air[1]) == std::vector<int64_t>{3, 4, 6, 7, 3, 4, 6});
        CHECK(gaps(both) == std::vector<int64_t>{2, 3, 4, 2, 4, 5, 2, 3, 4, 2, 4});
    }
}

// C.2.5 asks it of Traffic and Status: every transmission the schedule makes changes channel.
TEST_CASE("transmit cycle: positions and Status alternate channel, names keep to B every 10 s") {
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        for (const bool airborne : {true, false}) {
            CAPTURE(serial);
            CAPTURE(airborne);
            Transmitter transmitter = transmitter_for(serial);
            int last_slot[Transmitter::kPayloads] = {-1, -1, -1};
            uint32_t last_s[Transmitter::kPayloads] = {};
            int sent[Transmitter::kPayloads] = {};
            for (const Burst& burst : flown(transmitter, kMinuteStartS, 60, airborne)) {
                const int type = static_cast<int>(burst.payload);
                const uint32_t gap = burst.utc_s - last_s[type];
                if (burst.payload == Payload::Callsign) {
                    CHECK(burst.slot == 1);
                    if (sent[type] > 0) CHECK(gap == 10);
                } else if (last_slot[type] >= 0) {
                    CHECK(burst.slot != last_slot[type]);
                    if (burst.payload == Payload::Status || !airborne)
                        CHECK((gap == 9 || gap == 11));
                }
                last_slot[type] = burst.slot;
                last_s[type] = burst.utc_s;
                sent[type]++;
            }
            CHECK(sent[0] == (airborne ? 60 : 6));
            CHECK(sent[1] == 6);
            CHECK(sent[2] == 6);
        }
    }
}

// A takeoff or a landing changes nothing about the extras, and no extra shares a position's dwell.
TEST_CASE(
    "transmit cycle: airborne the extras keep the ground's seconds, apart from the position") {
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        CAPTURE(serial);
        Transmitter air = transmitter_for(serial);
        Transmitter ground = transmitter_for(serial);
        std::vector<Burst> air_extras;
        for (const Burst& burst : flown(air, kMinuteStartS, 60, true)) {
            if (burst.payload == Payload::Position) {
                CHECK(burst.slot == Transmitter::slot_in(burst.utc_s));
                continue;
            }
            CHECK(burst.slot == 1 - Transmitter::slot_in(burst.utc_s));
            air_extras.push_back(burst);
        }
        std::vector<Burst> ground_extras;
        for (const Burst& burst : flown(ground, kMinuteStartS, 60, false))
            if (burst.payload != Payload::Position) ground_extras.push_back(burst);
        REQUIRE(air_extras.size() == ground_extras.size());
        for (size_t i = 0; i < air_extras.size(); i++) {
            CHECK(air_extras[i].utc_s == ground_extras[i].utc_s);
            CHECK(air_extras[i].slot == ground_extras[i].slot);
            CHECK(air_extras[i].payload == ground_extras[i].payload);
        }
    }
}

// EN 300 220-1's off-time, and the executor's dwell: 100 ms apart on one frequency, one a dwell.
TEST_CASE("transmit cycle: no dwell owes two bursts, and one frequency's bursts are 100 ms apart") {
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        for (const bool airborne : {true, false}) {
            CAPTURE(serial);
            CAPTURE(airborne);
            Transmitter transmitter = transmitter_for(serial);
            check_spacing(flown(transmitter, kMinuteStartS, 60, airborne));
        }
    }
}

// A takeoff at +7 and a landing at +33 move no extra, and nothing ever crowds a dwell.
TEST_CASE("transmit cycle: a takeoff or a landing mid-cycle keeps every dwell to one burst") {
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        CAPTURE(serial);
        Transmitter transmitter = transmitter_for(serial);
        const uint32_t c = kMinuteStartS + transmitter.cycle_start();
        const auto in_flight = [c](uint32_t utc_s) { return utc_s >= c + 7 && utc_s < c + 33; };
        check_spacing(flown(transmitter, c, 60, in_flight));
    }
}

// +0 is retried at +2, +4, +6 and +8, +9 at +11 to +19, each time on the channel it missed.
TEST_CASE("transmit cycle: a missed ground position is retried 2 s on, on the same channel") {
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        CAPTURE(serial);
        for (const uint32_t missed : {uint32_t{0}, uint32_t{9}}) {
            CAPTURE(missed);
            const uint32_t attempts = missed == 0 ? 5 : 6;
            for (uint32_t refusals = 1; refusals <= attempts; refusals++) {
                Transmitter transmitter = transmitter_for(serial);
                const uint32_t c = kMinuteStartS + transmitter.cycle_start();
                const uint32_t turn = c + missed;
                const auto refused = [turn, refusals](uint32_t utc_s, Payload payload) {
                    return payload == Payload::Position && utc_s >= turn &&
                           utc_s < turn + 2 * refusals && (utc_s - turn) % 2 == 0;
                };
                const std::vector<Burst> bursts =
                    flown(transmitter, c, Transmitter::kCycleS, false, refused);
                const std::vector<Burst> positions = of_type(bursts, Payload::Position);
                if (refusals < attempts) {
                    REQUIRE(positions.size() == 2);
                    const Burst& retried = missed == 0 ? positions[0] : positions[1];
                    CHECK(retried.utc_s == turn + 2 * refusals);
                    CHECK(retried.slot == Transmitter::slot_in(turn));
                } else {
                    CHECK(positions.size() == 1);
                }
                check_spacing(bursts);
            }
        }
    }
}

// The Status at +2 would retry at +4, where the name holds B's dwell, so it goes at +6.
TEST_CASE("transmit cycle: a retry skips a second whose dwell already carries one of ours") {
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        CAPTURE(serial);
        Transmitter transmitter = transmitter_for(serial);
        const uint32_t c = kMinuteStartS + transmitter.cycle_start();
        const auto refused = [c](uint32_t utc_s, Payload payload) {
            return payload == Payload::Status && utc_s == c + 2;
        };
        const std::vector<Burst> bursts =
            flown(transmitter, c, Transmitter::kCycleS, false, refused);
        const std::vector<Burst> statuses = of_type(bursts, Payload::Status);
        REQUIRE(statuses.size() == 2);
        CHECK(statuses[0].utc_s == c + 6);
        CHECK(statuses[0].slot == 1);
        CHECK(statuses[1].utc_s == c + 11);
        check_spacing(bursts);
    }
}

// The name missed at +4 goes out in the tail of +6, and its turn at +14 comes round as usual.
TEST_CASE("transmit cycle: a missed name is retried in the tail 2 s on") {
    for (uint32_t serial = 1; serial <= 1024; serial++) {
        CAPTURE(serial);
        Transmitter transmitter = transmitter_for(serial);
        const uint32_t c = kMinuteStartS + transmitter.cycle_start();
        const auto refused = [c](uint32_t utc_s, Payload payload) {
            return payload == Payload::Callsign && utc_s == c + 4;
        };
        const std::vector<Burst> bursts =
            flown(transmitter, c, Transmitter::kCycleS, false, refused);
        const std::vector<Burst> names = of_type(bursts, Payload::Callsign);
        REQUIRE(names.size() == 2);
        CHECK(names[0].utc_s == c + 6);
        CHECK(names[0].at_ms >= kCallsignStartMs);
        CHECK(names[1].utc_s == c + 14);
        check_spacing(bursts);
    }
}

// Positions at +0 and +9 never go out: the first is counted when +9 opens, the second at +20.
TEST_CASE("transmit cycle: a turn that never went out is given up and counted at the next turn") {
    Transmitter transmitter = transmitter_for(1);
    const uint32_t c = kMinuteStartS + transmitter.cycle_start();
    uint32_t given_up = 0;
    for (uint32_t utc_s = c; utc_s < c + Transmitter::kCycleS; utc_s++) {
        given_up += transmitter.give_up(utc_s, false, true);
        for (const int slot : {0, 1}) {
            for (const Payload payload : {Payload::Status, Payload::Callsign}) {
                const Transmitter::Attempt attempt =
                    transmitter.attempt(slot_plan(slot), utc_s, utc_s * 1000, false, 0, payload);
                if (attempt.go) transmitter.sent(utc_s, utc_s * 1000, payload);
            }
        }
    }
    CHECK(given_up == 1);
    CHECK(transmitter.give_up(c + Transmitter::kCycleS, false, true) == 1);
    CHECK(transmitter.give_up(c + Transmitter::kCycleS, false, true) == 0);
}
