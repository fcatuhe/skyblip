// The bus is the only path between a producer and a service, so its overflow
// behaviour is a safety property: a dropped message must be counted, never
// silently swallowed, and the queue must not corrupt when it wraps.
#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "doctest/doctest.h"

using namespace skyblip;

TEST_CASE("bus: a queue round-trips in order") {
    bus::Queue<int, 4> queue;
    CHECK(queue.empty());
    for (int i = 1; i <= 4; i++) CHECK(queue.push(i));

    int out = 0;
    for (int i = 1; i <= 4; i++) {
        REQUIRE(queue.pop(out));
        CHECK(out == i);
    }
    CHECK(queue.empty());
    CHECK_FALSE(queue.pop(out));
}

TEST_CASE("bus: pushing past capacity drops the new item and counts it") {
    bus::Queue<int, 2> queue;
    CHECK(queue.push(1));
    CHECK(queue.push(2));
    CHECK_FALSE(queue.push(3));
    CHECK(queue.dropped() == 1);

    int out = 0;
    REQUIRE(queue.pop(out));
    CHECK(out == 1);  // the OLD items survive: a queue is not a ring buffer here
    CHECK(queue.push(4));
}

// Each round moves the 16-bit counters by four, so 20000 rounds cross their wrap past 65535 with
// the queue full.
TEST_CASE("bus: the counters wrap past their range without losing a slot or an item") {
    bus::Queue<int, 4> queue;
    int out = 0;
    bool ordered = true;
    for (int round = 0; round < 20000; round++) {
        for (int i = 0; i < 4; i++) ordered = queue.push(round * 4 + i) && ordered;
        ordered = !queue.push(-1) && ordered;
        for (int i = 0; i < 4; i++) ordered = queue.pop(out) && out == round * 4 + i && ordered;
        ordered = queue.empty() && ordered;
    }
    CHECK(ordered);
    CHECK(queue.dropped() == 20000);
}

TEST_CASE("bus: the traffic clock falls back to uptime before the first UTC") {
    bus::State state;
    state.own.utc_valid = false;
    CHECK(state.traffic_now_s(12500) == 12);
    state.own.utc_valid = true;
    state.own.utc_s = 43200;
    CHECK(state.traffic_now_s(12500) == 43200);
}

// A receiver names a second hundreds of milliseconds after the edge that opened it.
TEST_CASE("bus: the traffic clock is the edge's second, not the sentence's") {
    bus::State state;
    state.own.utc_valid = true;
    state.own.utc_s = 43200;
    state.clock.pps_locked = true;
    state.clock.utc_s = 43201;
    CHECK(state.traffic_now_s(12500) == 43201);

    // A lost lock leaves nothing to carry the second forward, and the sentence has it.
    state.clock.pps_locked = false;
    CHECK(state.traffic_now_s(12500) == 43200);
}
