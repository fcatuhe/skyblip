// The PPS word on the diagnostics pages, read off the glass against the slot map's own holdover.
#include "core/timing/slot.h"
#include "test/support/glass_text.h"
#include "test/support/screen_rig.h"

namespace {

constexpr uint32_t kSecondMs = 1000;

constexpr uint64_t kLastEdgeUs = 12'000'000;

void edge_lost(Rig& rig, uint32_t ms_ago, bool locked) {
    rig.state.clock.utc_valid = true;
    rig.state.clock.pps_locked = locked;
    rig.state.clock.ms_since_pps = ms_ago;
    rig.state.clock.pps_edge_us = kLastEdgeUs;
}

bool radio_log_reads(Rig& rig, const char* text) {
    return reads_in(rig.chip.framebuffer(), text, 0, 10, 200, 24);
}

bool raw_reads(Rig& rig, const char* text) {
    return reads_in(rig.chip.framebuffer(), text, 0, 68, 200, 85);
}

}  // namespace

// Pps::locked() holds for 3 s, so a late edge is still anchored and still transmitting.
TEST_CASE("screen pps: an edge 2 s old on a locked clock reads LOCK on both pages") {
    Rig rig;
    uint32_t t = 0;
    edge_lost(rig, 2 * kSecondMs, true);

    rig.show(t, go::Page::RadioLog);
    rig.run_seconds(t, 3);
    CHECK(radio_log_reads(rig, "PPS LOCK"));

    rig.show(t, go::Page::Raw);
    rig.run_seconds(t, 3);
    CHECK(raw_reads(rig, "PPS LOCK 2000MS"));
}

TEST_CASE("screen pps: an edge 10 s old reads HOLD with its age while the slot map flies on") {
    Rig rig;
    uint32_t t = 0;
    edge_lost(rig, 10 * kSecondMs, false);
    REQUIRE(timing::in_pps_holdover(rig.state.clock));

    rig.show(t, go::Page::RadioLog);
    rig.run_seconds(t, 3);
    CHECK(radio_log_reads(rig, "PPS HOLD 10"));

    rig.show(t, go::Page::Raw);
    rig.run_seconds(t, 3);
    CHECK(raw_reads(rig, "PPS HOLD 10000MS"));
}

TEST_CASE("screen pps: an edge 61 s old is past holdover and reads NONE") {
    Rig rig;
    uint32_t t = 0;
    edge_lost(rig, 61 * kSecondMs, false);
    REQUIRE(timing::kPpsHoldoverMs < 61 * kSecondMs);

    rig.show(t, go::Page::RadioLog);
    rig.run_seconds(t, 3);
    CHECK(radio_log_reads(rig, "PPS NONE"));

    rig.show(t, go::Page::Raw);
    rig.run_seconds(t, 3);
    CHECK(raw_reads(rig, "PPS NONE"));
}

// Pps::ms_since() reads 0 before the first edge, which the pages once showed as HOLD 0.
TEST_CASE("screen pps: a clock with UTC and no edge ever reads NONE, and transmits nothing") {
    Rig rig;
    uint32_t t = 0;
    edge_lost(rig, 0, false);
    rig.state.clock.pps_edge_us = 0;
    for (int phase = 0; phase < 1000; phase++)
        REQUIRE_FALSE(timing::Scheduler::plan(phase, rig.state.clock).tx_allowed);

    rig.show(t, go::Page::RadioLog);
    rig.run_seconds(t, 3);
    CHECK(radio_log_reads(rig, "PPS NONE"));

    rig.show(t, go::Page::Raw);
    rig.run_seconds(t, 3);
    CHECK(raw_reads(rig, "PPS NONE 0MS"));
}
