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

namespace {

constexpr uint32_t kLastS = 1785628800;
constexpr uint64_t kLastEdgeUs = 1'000'000;

// A receiver gone silent with its PPS: L76k::poll withdraws the fix and leaves its UTC as it was.
bus::State silent_since_last_edge() {
    bus::State state;
    state.own.utc_valid = true;
    state.own.utc_s = kLastS;
    state.clock.utc_valid = true;
    state.clock.utc_s = kLastS;
    state.clock.utc_edge_us = kLastEdgeUs;
    return state;
}

uint32_t traffic_s_at(bus::State& state, uint32_t now_ms) {
    timing::take_edge(state.clock, kLastEdgeUs, uint64_t{now_ms} * 1000);
    return state.traffic_now_s(now_ms);
}

model::AircraftObservation heard_at(uint32_t at_s) {
    model::AircraftObservation observation{};
    observation.address = 0x424242;
    observation.address_mapping_table = 6;
    observation.position_valid = true;
    observation.latitude_1e7 = 481000000;
    observation.longitude_1e7 = 81000000;
    observation.source = model::Source::AdslDirect;
    observation.received.at_s = at_s;
    return observation;
}

}  // namespace

TEST_CASE("bus: past the PPS holdover the traffic clock counts on from the last edge") {
    bus::State state = silent_since_last_edge();
    CHECK(traffic_s_at(state, 1000 + 59'500) == kLastS + 59);
    CHECK(traffic_s_at(state, 1000 + 61'500) == kLastS + 61);
    CHECK(traffic_s_at(state, 1000 + 3'600'500) == kLastS + 3600);
}

TEST_CASE("bus: past the PPS holdover the traffic a silent receiver heard is kept, then ages out") {
    bus::State state = silent_since_last_edge();
    const uint32_t heard_s = traffic_s_at(state, 1000 + 59'500);
    REQUIRE(state.traffic.update(heard_at(heard_s), heard_s) >= 0);

    state.traffic.age_out(traffic_s_at(state, 1000 + 61'500));
    CHECK(state.traffic.find(6, 0x424242) >= 0);

    state.traffic.age_out(traffic_s_at(state, 1000 + 59'500 + 600'000));
    CHECK(state.traffic.find(6, 0x424242) < 0);
}

TEST_CASE("bus: past the PPS holdover a receiver that still talks names the second") {
    bus::State state = silent_since_last_edge();
    state.own.utc_s = kLastS + 120;
    CHECK(traffic_s_at(state, 1000 + 120'700) == kLastS + 120);
}
