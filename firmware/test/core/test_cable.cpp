// Cable mode: the unit follows the aircraft's master switch through the USB socket it is plugged
// into.
#include "core/power/cable.h"
#include "doctest/doctest.h"

using namespace skyblip::power;

TEST_CASE(
    "cable: a cable gone for the hold switches the unit off, and one that comes back does not" *
    doctest::test_suite("main")) {
    CableWatch watch;
    CHECK_FALSE(watch.gone(0, /*external_power=*/true, /*airborne=*/false));
    CHECK_FALSE(watch.gone(1000, false, false));
    CHECK_FALSE(watch.gone(1000 + kCableGoneHoldMs - 1, false, false));
    CHECK(watch.gone(1000 + kCableGoneHoldMs, false, false));

    // A cranking dip or a master switched off between two starts brings the cable back.
    CableWatch cranking;
    CHECK_FALSE(cranking.gone(0, true, false));
    CHECK_FALSE(cranking.gone(1000, false, false));
    CHECK_FALSE(cranking.gone(kCableGoneHoldMs, true, false));
    CHECK_FALSE(cranking.gone(kCableGoneHoldMs + 1000, false, false));
    CHECK_FALSE(cranking.gone(2 * kCableGoneHoldMs, false, false));
    CHECK(cranking.gone(kCableGoneHoldMs + 1000 + kCableGoneHoldMs, false, false));
}

// RESET wakes a unit in cable mode on its cell, and that unit must run until a press or the cutoff.
TEST_CASE("cable: a unit that has not seen the cable since it booted never counts it gone") {
    CableWatch watch;
    CHECK_FALSE(watch.gone(0, false, false));
    CHECK_FALSE(watch.gone(10 * kCableGoneHoldMs, false, false));
}

// The cell carries an airborne unit for hours, and a transmitter that stops in flight is the
// hazard.
TEST_CASE(
    "cable: a cable lost in flight switches nothing off until the hold has run on the ground" *
    doctest::test_suite("main")) {
    CableWatch watch;
    CHECK_FALSE(watch.gone(0, true, true));
    CHECK_FALSE(watch.gone(1000, false, true));
    CHECK_FALSE(watch.gone(1000 + 3 * kCableGoneHoldMs, false, true));

    const uint32_t landed_ms = 1000 + 3 * kCableGoneHoldMs;
    CHECK_FALSE(watch.gone(landed_ms, false, false));
    CHECK_FALSE(watch.gone(landed_ms + kCableGoneHoldMs - 1, false, false));
    CHECK(watch.gone(landed_ms + kCableGoneHoldMs, false, false));
}

TEST_CASE("cable: the countdown runs only while the hold does, down to zero") {
    CableWatch watch;
    CHECK_FALSE(watch.off_in_ms(0));
    watch.gone(0, true, false);
    CHECK_FALSE(watch.off_in_ms(0));
    watch.gone(1000, false, false);
    CHECK(watch.off_in_ms(1000) == kCableGoneHoldMs);
    CHECK(watch.off_in_ms(61000) == kCableGoneHoldMs - 60000);
    CHECK(watch.off_in_ms(1000 + kCableGoneHoldMs + 5) == 0u);
    watch.gone(2000, false, true);
    CHECK_FALSE(watch.off_in_ms(2000));
}

TEST_CASE("cable: the glass counts whole minutes up, so the last minute reads 1 and never 0") {
    CHECK(off_in_min(kCableGoneHoldMs) == 5);
    CHECK(off_in_min(kCableGoneHoldMs - 1) == 5);
    CHECK(off_in_min(4 * 60000) == 4);
    CHECK(off_in_min(60001) == 2);
    CHECK(off_in_min(1) == 1);
    CHECK(off_in_min(0) == 1);
}
