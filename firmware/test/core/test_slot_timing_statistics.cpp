// G6: the accumulator behind the bench PPS-to-lock histogram. Its two feeds -
// the latched PPS edge and an armed dwell's outcome - are exercised directly
// here on virtual time, exactly as core/timing/slot_timing_statistics.h promises: no
// heap, integer only, and no phase math to get wrong across the second wrap.
#include <limits>

#include "core/timing/slot_timing_statistics.h"
#include "doctest/doctest.h"

using namespace skyblip::timing;

TEST_CASE("timing statistics: a clean 1 Hz PPS train lands in the centre bucket") {
    SlotTimingStatistics statistics;
    uint64_t edge_us = 1000000;
    statistics.record_edge(edge_us, true);  // the first edge only seeds the clock
    for (int i = 0; i < 10; i++) {
        edge_us += 1000000;
        statistics.record_edge(edge_us, true);
    }
    CHECK(statistics.pps_samples() == 10);
    CHECK(statistics.pps_bucket(3) == 10);
    for (int b = 0; b < SlotTimingStatistics::kBuckets; b++)
        if (b != 3) CHECK(statistics.pps_bucket(b) == 0);
    CHECK(statistics.pps_worst_us() == 0);
    CHECK(statistics.holdover_events() == 0);
}

TEST_CASE("timing statistics: polling between edges never inflates the sample count") {
    SlotTimingStatistics statistics;
    statistics.record_edge(1000000, true);
    for (int i = 0; i < 50; i++) statistics.record_edge(1000000, true);  // same edge, many passes
    statistics.record_edge(2000000, true);
    CHECK(statistics.pps_samples() == 1);
    CHECK(statistics.pps_bucket(3) == 1);
}

// The three named edges, in order: inside the SX1262's own retune time (still
// the centre bucket - that error is the chip doing what its datasheet
// promises), past it but inside the hop guard, past that but inside the
// jitter guard, and past the whole budget.
TEST_CASE("timing statistics: a drifting edge walks the buckets outward, one guard at a time") {
    SlotTimingStatistics statistics;
    uint64_t edge_us = 0;
    statistics.record_edge(edge_us, true);

    const int64_t between_guards =
        (SlotTimingStatistics::kHopGuardUs + SlotTimingStatistics::kJitterGuardUs) / 2;
    const int64_t errors[] = {10, 200, between_guards, 8000};
    const int expect_late[] = {3, 4, 5, 6};
    for (int64_t error : errors) {
        edge_us += static_cast<uint64_t>(1000000 + error);
        statistics.record_edge(edge_us, true);
    }
    for (int b : expect_late) CHECK(statistics.pps_bucket(b) == 1);
    CHECK(statistics.pps_samples() == 4);
    CHECK(statistics.pps_worst_us() == 8000);

    SlotTimingStatistics early;
    edge_us = 0;
    early.record_edge(edge_us, true);
    const int expect_early[] = {3, 2, 1, 0};
    for (int64_t error : errors) {
        edge_us += static_cast<uint64_t>(1000000 - error);
        early.record_edge(edge_us, true);
    }
    for (int b : expect_early) CHECK(early.pps_bucket(b) == 1);
    CHECK(early.pps_worst_us() == -8000);
}

TEST_CASE("timing statistics: a lost PPS becomes holdover, not a phase outlier") {
    SlotTimingStatistics statistics;
    uint64_t edge_us = 0;
    statistics.record_edge(edge_us, true);
    edge_us += 1000000;
    statistics.record_edge(edge_us, true);
    REQUIRE(statistics.pps_samples() == 1);

    // The receiver drops lock and stays down for a while: nothing to report,
    // every pass the same as the last.
    statistics.record_edge(0, false);
    statistics.record_edge(0, false);
    statistics.record_edge(0, false);
    CHECK(statistics.holdover_events() == 1);  // one transition, not one per pass

    // Ten seconds pass before it comes back. That whole gap must not become a
    // sample: the histogram has nothing honest to say about it.
    edge_us += 10 * 1000000;
    statistics.record_edge(edge_us, true);
    CHECK(statistics.pps_samples() == 1);
    for (int b = 0; b < SlotTimingStatistics::kBuckets; b++)
        if (b != 3) CHECK(statistics.pps_bucket(b) == 0);

    // The second after reacquisition is measured normally again.
    edge_us += 1000000;
    statistics.record_edge(edge_us, true);
    CHECK(statistics.pps_samples() == 2);
    CHECK(statistics.pps_bucket(3) == 2);
}

TEST_CASE("timing statistics: an edge gap while still reporting locked is holdover too") {
    SlotTimingStatistics statistics;
    statistics.record_edge(0, true);
    statistics.record_edge(2200000, true);  // more than one edge went missing
    CHECK(statistics.holdover_events() == 1);
    CHECK(statistics.pps_samples() == 0);
}

// Slot 1's dwell opens at 800 ms and its tail runs to 200 ms past the second
// it started in (core/timing/slot.h kSlot1WrapMs). An armed deadline just before
// the wrap and a landed instant just after it are still only a few thousand
// microseconds apart - not a second, and not undefined, because nothing here
// is derived from a phase.
TEST_CASE("timing statistics: the dwell-phase error is exact across the UTC second wrap") {
    SlotTimingStatistics statistics;
    const uint64_t deadline_us = 999995000;
    const uint64_t landed_us = 1000001200;
    statistics.record_dwell_phase(static_cast<int64_t>(landed_us) -
                                  static_cast<int64_t>(deadline_us));
    CHECK(statistics.dwell_samples() == 1);
    CHECK(statistics.dwell_worst_us() == 6200);
    CHECK(statistics.dwell_bucket(SlotTimingStatistics::bucket_of(6200)) == 1);
}

TEST_CASE("timing statistics: an extreme error saturates the outer bucket, never indexes past it") {
    CHECK(SlotTimingStatistics::bucket_of(1000000000) == 6);
    CHECK(SlotTimingStatistics::bucket_of(-1000000000) == 0);

    SlotTimingStatistics statistics;
    statistics.record_dwell_phase(9000000000LL);  // far past anything this budget bounds
    CHECK(statistics.dwell_bucket(6) == 1);
    CHECK(statistics.dwell_worst_us() == std::numeric_limits<int32_t>::max());
}

TEST_CASE("timing statistics: missed and refused are counted apart from the phase histograms") {
    SlotTimingStatistics statistics;
    statistics.record_missed();
    statistics.record_missed();
    statistics.record_refused();
    CHECK(statistics.missed() == 2);
    CHECK(statistics.refused() == 1);
    CHECK(statistics.dwell_samples() == 0);
    CHECK(statistics.pps_samples() == 0);
    for (int b = 0; b < SlotTimingStatistics::kBuckets; b++) {
        CHECK(statistics.dwell_bucket(b) == 0);
        CHECK(statistics.pps_bucket(b) == 0);
    }
}

TEST_CASE("timing statistics: an out-of-range bucket index reads zero, not memory past the array") {
    SlotTimingStatistics statistics;
    statistics.record_dwell_phase(0);
    CHECK(statistics.dwell_bucket(-1) == 0);
    CHECK(statistics.dwell_bucket(SlotTimingStatistics::kBuckets) == 0);
}
