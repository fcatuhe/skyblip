// Own-ship on the running product: vertical speed from the barometer or from GNSS, the PPS edge,
// the first fix and how far the receiver has got, across the 49.7-day wrap as well.
#include <cstdlib>
#include <string>

#include "core/annunciation/pattern.h"
#include "core/events/link.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"

using namespace skyblip;

TEST_CASE("product: barometric pressure drives vertical speed") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    CHECK_FALSE(rig.product.ownship().baro_active());

    rig.push_baro(100000, 1000);
    rig.run(1000, 1000);
    CHECK(rig.product.ownship().baro_active());
    rig.push_baro(101000, 3000);
    rig.run(3000, 3000);

    // +10 m in 2 s
    CHECK(rig.state().own.climb_mm_s == doctest::Approx(5000).epsilon(0.05));
}

TEST_CASE("product: a baro sample inside the window is ignored, not extrapolated") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    rig.push_baro(100000, 1000);
    rig.run(1000, 1000);
    rig.push_baro(199000, 1100);
    rig.run(1100, 1100);
    CHECK(rig.state().own.climb_mm_s == 0);
}

TEST_CASE("product: with no barometer, vertical speed comes from GNSS") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);

    rig.push_fix(1000, 1);
    rig.run(1000, 1000);
    rig.push_fix(1010, 2);
    rig.run(3000, 3000);

    CHECK_FALSE(rig.product.ownship().baro_active());
    CHECK(rig.state().own.climb_mm_s == doctest::Approx(5000).epsilon(0.05));
}

TEST_CASE("product: once the barometer speaks, GNSS stops setting vertical speed") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);

    rig.push_baro(100000, 500);
    rig.run(500, 500);
    rig.push_baro(100200, 1500);  // +2 m in 1 s
    rig.run(1500, 1500);
    const int32_t from_baro = rig.state().own.climb_mm_s;
    CHECK(from_baro == doctest::Approx(2000).epsilon(0.1));

    rig.push_fix(1000, 1);
    rig.run(2000, 2000);
    rig.push_fix(1200, 2);
    rig.run(4000, 4000);

    CHECK(rig.state().own.climb_mm_s == from_baro);
}

// Once active the barometer stayed in charge for ever, and a dead one froze its last climb on air.
TEST_CASE("product: a barometer that stops answering hands vertical speed back to GNSS") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    rig.push_baro(100000, 500);
    rig.run(500, 500);
    rig.push_baro(100200, 1500);  // +2 m in 1 s
    rig.run(1500, 1500);
    REQUIRE(rig.product.ownship().baro_active());

    rig.push_fix(1000, 1);
    rig.run(6000, 6000);
    rig.push_fix(1010, 2);
    rig.run(8000, 8000);

    CHECK_FALSE(rig.product.ownship().baro_active());
    CHECK(rig.state().own.climb_valid);
    // +10 m in 2 s of GNSS
    CHECK(rig.state().own.climb_mm_s == doctest::Approx(5000).epsilon(0.05));
}

TEST_CASE("product: with neither a fix nor a barometer the climb is not valid") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    rig.push_fix(1000, 1);
    rig.run(1000, 1000);
    rig.push_fix(1010, 2);
    rig.run(3000, 3000);
    REQUIRE(rig.state().own.climb_valid);

    rig.product.bus().gnss.push(gnss::GnssSolution{});
    rig.run(4000, 4000);
    CHECK_FALSE(rig.state().own.climb_valid);
}

// A 2D fix differentiated the height it held and sent a climb of zero as valid.
TEST_CASE("product: a 2D fix goes on air with its climb marked unavailable (ADS-L G.1.9)") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    rig.push_fix(1000, 1);
    rig.run(1000, 1000);
    rig.push_fix(1010, 2);
    rig.run(3000, 3000);
    REQUIRE(rig.state().own.climb_valid);

    rig.push_2d_fix(1010, 3);
    rig.run(4000, 4000);
    CHECK(rig.state().own.fix_valid);
    CHECK_FALSE(rig.state().own.climb_valid);
    protocol::AdslPacket burst{};
    protocol::from_own(burst, rig.state().own, rig.product.board().roles().device_addr, 6, 4);
    CHECK_FALSE(burst.has_climb());
}

// The climb reference outlived a 2D spell, and the height 3D came back with read as a climb.
TEST_CASE("product: the height a 3D fix returns with after a 2D spell is not a climb") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t updates = 0;
    int32_t steepest_mm_s = 0;
    const auto apply_at = [&](uint32_t t) {
        rig.run(t, t);
        const model::OwnState& own = rig.state().own;
        if (own.climb_valid && std::abs(own.climb_mm_s) > steepest_mm_s)
            steepest_mm_s = std::abs(own.climb_mm_s);
    };

    uint32_t t = 1000;
    for (; t <= 3000; t += 1000) {
        rig.push_fix(1000, ++updates);
        apply_at(t);
    }
    for (; t <= 6000; t += 1000) {
        rig.push_2d_fix(1000, ++updates);
        apply_at(t);
    }
    for (; t <= 12000; t += 1000) {
        rig.push_fix(1100, ++updates);
        apply_at(t);
    }

    CHECK(steepest_mm_s == 0);
    CHECK(rig.state().own.climb_valid);
}

// B3. The slot map is specified against the PPS edge, and the transmit plan is
// armed from it. An edge rebuilt from the millisecond phase is up to a
// millisecond late on a 5 ms guard.
TEST_CASE("product: the PPS edge on the bus is the edge itself, to the microsecond") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);

    rig.platform.clock().set_micros(12345678);
    rig.product.step(12345);
    CHECK(rig.state().clock.pps_locked);
    CHECK(rig.state().clock.pps_edge_us == 12000000u);
    CHECK(rig.state().clock.ms_since_pps == 345u);

    // What the same pass used to publish, rebuilt from the phase: the sub-
    // millisecond remainder was thrown away, and it is not a rounding error but
    // a signed lateness on every plan armed from it.
    const uint64_t rebuilt =
        12345678u - static_cast<uint64_t>(rig.state().clock.ms_since_pps) * 1000;
    CHECK(rebuilt - rig.state().clock.pps_edge_us == 678u);

    // No lock, no edge: a stale instant is worse than none, because the phase
    // it implies is a plausible one.
    rig.platform.pps().set_locked(false);
    rig.platform.clock().set_micros(13345678);
    rig.product.step(13345);
    CHECK_FALSE(rig.state().clock.pps_locked);
    CHECK(rig.state().clock.pps_edge_us == 0u);
}

TEST_CASE("product: the first fix is announced once, then own-ship settles before it flies") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 500);
    CHECK_FALSE(rig.product.ownship().first_fix().ever_fixed());
    CHECK(int(rig.platform.annunciator().level()) == 0);

    rig.push_fix(1000, 1);
    uint16_t first_note_hz = 0;
    for (uint32_t t = 550; t <= 700; t += 10) {
        rig.platform.clock().set_millis(t);
        rig.product.step(t);
        if (first_note_hz == 0) first_note_hz = rig.platform.annunciator().hz();
    }
    CHECK(rig.product.ownship().first_fix().ever_fixed());
    CHECK(first_note_hz == annunciation::kFirstFixJingle[0].hz);
    // The motor stays out of it: haptics mean traffic that escalated.
    CHECK(rig.platform.annunciator().haptic_ms() == 0);

    // It is short and it stops by itself, so nothing has to remember to silence
    // it before the first traffic contact arrives.
    rig.run(750, 3000);
    CHECK_FALSE(rig.platform.annunciator().sounding());

    // Nothing is worth transmitting yet, and 20 s later it is.
    const gnss::FirstFix& fix = rig.product.ownship().first_fix();
    CHECK_FALSE(fix.settled(3000));
    CHECK_FALSE(fix.settled(fix.fix_since_ms() + gnss::kFirstFixSettleMs - 1));
    CHECK(fix.settled(fix.fix_since_ms() + gnss::kFirstFixSettleMs));

    // A second acquisition is not a first one: no second chirp, and the shorter
    // wait applies.
    gnss::GnssSolution lost{};
    lost.fix_valid = false;
    rig.product.bus().gnss.push(lost);
    rig.run(3050, 3200);
    CHECK_FALSE(fix.settled(3200));
    rig.push_fix(1000, 2);
    rig.run(3250, 3400);
    CHECK_FALSE(rig.platform.annunciator().sounding());
    CHECK(fix.settled(fix.fix_since_ms() + gnss::kRefixSettleMs));
}

// M. Vertical speed and turn rate are both differences over a window kept on
// ports::Clock::millis(), and both keep the instant they last sampled at in a
// uint32_t whose zero is biased away rather than flagged (ownship.cpp: a stamp of
// zero would mean "no reference yet", and the counter produces exactly one zero
// per wrap). The windows themselves are unsigned differences, so this is what a
// climb through the wrap instant looks like: a climb, measured over its window,
// not a 4.29-billion-millisecond interval that reads as no climb at all.
TEST_CASE("product: vertical speed is measured across the 49.7-day wrap") {
    Rig rig{kBaroByHand};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0u - 500u;  // half a second short of the wrap

    rig.push_baro(100000, t);
    rig.run_span(t, 0);
    CHECK(rig.product.ownship().baro_active());

    // +10 m over the second that straddles zero.
    const uint32_t after = t + 950u;
    rig.push_baro(101000, after);
    t = after;
    rig.run_span(t, 0);
    CHECK(rig.state().own.climb_mm_s == doctest::Approx(10000).epsilon(0.1));

    // And a sample inside the window is still refused after the wrap, rather than
    // being taken as a 49-day interval that reads as no climb at all.
    rig.push_baro(199000, t);
    rig.run_span(t, 0);
    CHECK(rig.state().own.climb_mm_s == doctest::Approx(10000).epsilon(0.1));
}

// M. Where the radio believes it is inside the second, across the 49.7-day wrap of
// ports::Clock::millis(). With PPS locked the phase is measured from the latched
// edge, which is a 64-bit microsecond figure and cannot wrap in the life of the
// device. Without it the phase used to be now_ms % 1000, and that is not a phase at
// all: 2^32 ms is 4294967.296 seconds, so at the wrap the free-running second
// stepped 705 ms BACKWARDS and the dwell map was armed out of order for a second -
// with the anchor already lost, which is the worst moment to add a fault. Both
// branches read micros() now.
//
// The clock is driven in microseconds here because that is what the silicon does:
// now_ms is the low 32 bits of the same uptime, so millis() wraps underneath a
// micros() that keeps counting.
TEST_CASE("product: the free-running dwell phase steps forward through the 49.7-day wrap") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint64_t us = static_cast<uint64_t>(0xFFFFFF00u) * 1000u;  // 256 ms short of the wrap

    // The receiver has no PPS lock, so the case is about the free-running fallback
    // and not about the anchored path.
    rig.platform.pps().set_locked(false);
    rig.run_span_from_us(us, 0);
    REQUIRE_FALSE(rig.state().clock.pps_locked);

    int previous = rig.state().rf.dwell.phase_ms;
    bool wrapped = false;
    for (int step = 0; step < 20; step++) {
        const uint32_t millis_before = rig.platform.clock().millis();
        rig.run_span_from_us(us, 0);
        if (rig.platform.clock().millis() < millis_before) wrapped = true;
        // 50 ms of clock is 50 ms of phase, all the way round the second and all
        // the way through the counter turning over.
        CHECK(rig.state().rf.dwell.phase_ms == (previous + 50) % 1000);
        previous = rig.state().rf.dwell.phase_ms;
    }
    CHECK(wrapped);
}

// A pilot on the apron asks how long this will take, and NO FIX is the same word
// for a receiver reading satellites and one that has said nothing at all.
TEST_CASE("product: with no fix the bus says how far the receiver has got") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);

    rig.run(0, 100);
    CHECK(rig.state().gnss.stage == gnss::Stage::Silent);

    gnss::GnssSolution looking{};
    rig.product.bus().gnss.push(looking);
    rig.run(150, 200);
    CHECK(rig.state().gnss.stage == gnss::Stage::Blind);

    // A date is read off a satellite, so it is the first evidence of one.
    gnss::GnssSolution dated{};
    dated.utc_valid = true;
    dated.utc = Rig::kUtcBase;
    rig.product.bus().gnss.push(dated);
    rig.run(250, 300);
    CHECK(rig.state().gnss.stage == gnss::Stage::Solving);
    CHECK(rig.state().gnss.stage_s == 0u);

    rig.push_timed_fix(0, 500);
    rig.run(350, 400);
    CHECK(rig.state().gnss.stage == gnss::Stage::Fixed);

    // A receiver that stops talking is silent again, whatever it last managed.
    rig.run(450, 450 + gnss::kSentenceMaxAgeMs);
    CHECK(rig.state().gnss.stage == gnss::Stage::Silent);
}
