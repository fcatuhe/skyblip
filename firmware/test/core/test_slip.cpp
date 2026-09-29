// The ball: where a damped mass hangs in the specific force the case measures.
#include "core/flight/slip.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

constexpr int16_t kLevelFlightUpMg = flight::kLevelFlightMg;

flight::SpecificForce force_of(int16_t right_mg, int16_t up_mg = kLevelFlightUpMg,
                               int16_t aft_mg = 0) {
    return flight::SpecificForce{right_mg, up_mg, aft_mg};
}

int16_t slip_of(int16_t right_mg, int16_t up_mg = kLevelFlightUpMg, int16_t aft_mg = 0) {
    int16_t out = 0;
    REQUIRE(flight::slip_from_specific_force(force_of(right_mg, up_mg, aft_mg), out));
    return out;
}

}  // namespace

TEST_CASE("slip: coordinated flight reads zero whatever the load factor") {
    CHECK(slip_of(0) == 0);
    CHECK(slip_of(0, 2000) == 0);
    CHECK(slip_of(0, 500) == 0);
}

TEST_CASE("slip: the ball slides away from the force, the way the pilot does") {
    CHECK(slip_of(-200) > 0);
    CHECK(slip_of(200) < 0);
}

TEST_CASE("slip: the reading is the fraction of the resultant, not of one axis") {
    // 200 mg across a 1000 mg resultant is a fifth of it, the page's full scale.
    CHECK(slip_of(-200, 980) == doctest::Approx(200).epsilon(0.02));
    // The same force under a 2 g pull: a heavier ball hangs straighter.
    CHECK(slip_of(-200, 2000) == doctest::Approx(100).epsilon(0.05));
}

TEST_CASE("slip: free fall has nothing for a ball to hang from") {
    int16_t out = 42;
    CHECK_FALSE(flight::slip_from_specific_force(force_of(10, 10, 10), out));
    CHECK(out == 42);
}

TEST_CASE("slip: the ball is damped, so turbulence does not throw it across the cage") {
    flight::SlipBall ball;
    for (int i = 0; i < 40; i++) ball.update(force_of(0), 100 + i * 80u);
    REQUIRE(ball.mg() == 0);

    ball.update(force_of(-200), 3300);
    CHECK(ball.mg() > 0);
    CHECK(ball.mg() < 100);

    for (int i = 0; i < 40; i++) ball.update(force_of(-200), 3400 + i * 80u);
    // Within a pixel of full scale: the ball's travel is 12.5 mg to the pixel.
    CHECK(ball.mg() > flight::kSlipFullScaleMg - 13);
    CHECK(ball.mg() <= flight::kSlipFullScaleMg);
}

// The glass shows a frame a second: a ball still travelling at the next one is lag a pilot sees.
TEST_CASE("slip: the ball is within a pixel of a step before the glass shows its next frame") {
    constexpr uint32_t kSampleMs = 80;
    constexpr uint32_t kFrameMs = 1000;
    flight::SlipBall ball;
    for (int i = 0; i < 40; i++) ball.update(force_of(0), 100 + i * kSampleMs);
    REQUIRE(ball.mg() == 0);

    for (uint32_t at = 0; at < kFrameMs; at += kSampleMs) ball.update(force_of(-200), 3300 + at);
    CHECK(ball.mg() > flight::kSlipFullScaleMg - 13);
}

TEST_CASE("slip: the ball is damped, the sample it was damped from is not") {
    flight::SlipBall ball;
    for (int i = 0; i < 40; i++) ball.update(force_of(0), 100 + i * 80u);
    ball.update(force_of(-200), 3300);
    CHECK(ball.mg() < 100);
    CHECK(ball.measured_mg() == slip_of(-200));
}

TEST_CASE("slip: a sensor that stops answering takes the ball with it") {
    flight::SlipBall ball;
    CHECK_FALSE(ball.valid(0));

    ball.update(force_of(-100), 1000);
    CHECK(ball.valid(1000));
    CHECK(ball.valid(1000 + flight::kIndicatedStaleMs - 1));
    CHECK_FALSE(ball.valid(1000 + flight::kIndicatedStaleMs));
}
