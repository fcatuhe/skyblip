// F5. The first solution is what a pilot wants confirmed and what is least worth transmitting.
#include "core/flight/state.h"
#include "core/gnss/first_fix.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::gnss;

namespace {

// A solution the extrapolation model predicted to `residual_m` L1 metres, height solved.
Convergence predicted(uint16_t residual_m) {
    Convergence convergence;
    convergence.fix_valid = true;
    convergence.residual_valid = true;
    convergence.height_solved = true;
    convergence.residual_m = residual_m;
    return convergence;
}

// The first fix of a run: no previous fix to have predicted it from.
Convergence unmeasured() {
    Convergence convergence;
    convergence.fix_valid = true;
    return convergence;
}

Convergence no_fix() { return Convergence{}; }

void hold(FirstFix& first_fix, const Convergence& convergence, uint32_t from_ms, int seconds) {
    for (int i = 0; i < seconds; i++)
        first_fix.observe(convergence, from_ms + static_cast<uint32_t>(i) * 1000);
}

}  // namespace

TEST_CASE("first fix: the confirmation fires once, on the first solution ever") {
    FirstFix first_fix;
    CHECK_FALSE(first_fix.ever_fixed());
    CHECK_FALSE(first_fix.take_acquired());

    first_fix.observe(no_fix(), 1000);
    CHECK_FALSE(first_fix.take_acquired());

    first_fix.observe(unmeasured(), 5000);
    CHECK(first_fix.ever_fixed());
    CHECK(first_fix.take_acquired());
    CHECK_FALSE(first_fix.take_acquired());  // consumed: nothing can chirp twice

    // A fix held over many ticks is still one acquisition.
    first_fix.observe(predicted(1), 6000);
    first_fix.observe(predicted(1), 7000);
    CHECK_FALSE(first_fix.take_acquired());

    // Getting it back after losing it is not a second acquisition either.
    first_fix.observe(no_fix(), 8000);
    first_fix.observe(unmeasured(), 9000);
    CHECK_FALSE(first_fix.take_acquired());
}

TEST_CASE("first fix: a receiver whose solutions still walk waits out the clock") {
    FirstFix first_fix;
    first_fix.observe(unmeasured(), 1000);
    CHECK(first_fix.fix_since_ms() == 1000u);
    CHECK_FALSE(first_fix.settled(1000));

    // Forty metres of prediction error every second: solving, and still moving.
    hold(first_fix, predicted(40), 2000, 18);
    CHECK(first_fix.converged_fixes() == 0);
    CHECK_FALSE(first_fix.settled(1000 + kFirstFixSettleMs - 1));
    CHECK(first_fix.settled(1000 + kFirstFixSettleMs));
    CHECK(first_fix.settled(1000 + kFirstFixSettleMs + 60000));
}

TEST_CASE("first fix: three solutions the model predicted settle it before the clock does") {
    FirstFix first_fix;
    first_fix.observe(unmeasured(), 1000);

    first_fix.observe(predicted(kSettleResidualM - 1), 2000);
    CHECK_FALSE(first_fix.settled(2000));
    first_fix.observe(predicted(kSettleResidualM - 1), 3000);
    CHECK_FALSE(first_fix.settled(3000));
    first_fix.observe(predicted(kSettleResidualM - 1), 4000);
    CHECK(first_fix.converged_fixes() == kConvergedFixes);
    CHECK(first_fix.settled(4000));
    // Three seconds, where the clock alone would have taken twenty.
    CHECK(4000 - 1000 < kFirstFixSettleMs);
}

TEST_CASE("first fix: one solution over the threshold takes the count back to nothing") {
    FirstFix first_fix;
    first_fix.observe(unmeasured(), 1000);
    first_fix.observe(predicted(1), 2000);
    first_fix.observe(predicted(1), 3000);
    REQUIRE(first_fix.converged_fixes() == 2);

    first_fix.observe(predicted(kSettleResidualM), 4000);
    CHECK(first_fix.converged_fixes() == 0);
    CHECK_FALSE(first_fix.settled(4000));

    first_fix.observe(predicted(1), 5000);
    first_fix.observe(predicted(1), 6000);
    CHECK_FALSE(first_fix.settled(6000));
    first_fix.observe(predicted(1), 7000);
    CHECK(first_fix.settled(7000));
}

TEST_CASE("first fix: a 2D solution never counts as converged") {
    FirstFix first_fix;
    first_fix.observe(unmeasured(), 1000);

    Convergence flat = predicted(1);
    flat.height_solved = false;  // no VDOP: the receiver never solved for height
    hold(first_fix, flat, 2000, 10);
    CHECK(first_fix.converged_fixes() == 0);
    CHECK_FALSE(first_fix.settled(11000));
    CHECK(first_fix.settled(1000 + kFirstFixSettleMs));
}

TEST_CASE("first fix: losing the fix unsettles it, and the way back is shorter") {
    FirstFix first_fix;
    first_fix.observe(unmeasured(), 1000);
    REQUIRE(first_fix.settled(1000 + kFirstFixSettleMs));

    // A receiver that blinked has nothing settled to offer while it is out.
    first_fix.observe(no_fix(), 40000);
    CHECK_FALSE(first_fix.settled(40000));
    CHECK_FALSE(first_fix.settled(90000));

    // Back with a fix: the almanac and the filters are warm, so the second wait is the short one.
    first_fix.observe(unmeasured(), 50000);
    CHECK_FALSE(first_fix.settled(50000 + kRefixSettleMs - 1));
    CHECK(first_fix.settled(50000 + kRefixSettleMs));
    CHECK(kRefixSettleMs < kFirstFixSettleMs);
}

TEST_CASE("first fix: a fix lost mid-count starts the count again") {
    FirstFix first_fix;
    first_fix.observe(unmeasured(), 1000);
    first_fix.observe(predicted(1), 2000);
    first_fix.observe(predicted(1), 3000);
    REQUIRE(first_fix.converged_fixes() == 2);

    first_fix.observe(no_fix(), 4000);
    CHECK(first_fix.converged_fixes() == 0);

    first_fix.observe(unmeasured(), 5000);
    first_fix.observe(predicted(1), 6000);
    first_fix.observe(predicted(1), 7000);
    CHECK_FALSE(first_fix.settled(7000));
    first_fix.observe(predicted(1), 8000);
    CHECK(first_fix.settled(8000));
}

TEST_CASE("first fix: the settling clock survives a millisecond counter that wraps") {
    FirstFix first_fix;
    const uint32_t near_wrap = 0xFFFFFF00u;
    first_fix.observe(unmeasured(), near_wrap);
    CHECK_FALSE(first_fix.settled(near_wrap + 1000));
    CHECK(first_fix.settled(near_wrap + kFirstFixSettleMs));  // wraps through zero
}
