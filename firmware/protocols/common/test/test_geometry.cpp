// The integer geometry the stacks place a target with: a sine a quadrant out or a root that rounds
// down puts a target in the wrong place, with nothing logged.
#include <cstdint>
#include <cstdlib>

#include "doctest/doctest.h"
#include "protocols/common/geometry.h"

using namespace skyblip::wire;

TEST_CASE("geometry: sine/cosine magnitude and quadrature") {
    CHECK(std::abs(sine(0)) <= 1);
    CHECK(std::abs(sine(0x4000) - 0x4000) <= 2);  // sin(90) == 1.0
    CHECK(std::abs(cosine(0) - 0x4000) <= 2);     // cos(0) == 1.0
    CHECK(std::abs(sine(int16_t(0x8000))) <= 2);  // sin(180) == 0
    // sin^2 + cos^2 ~= 1 (scaled to 0x4000^2)
    for (int angle = 0; angle < 0x10000; angle += 997) {
        int32_t sine_value = sine(int16_t(angle));
        int32_t cosine_value = cosine(int16_t(angle));
        int32_t magnitude =
            (sine_value * sine_value + cosine_value * cosine_value) >> 14;  // ~0x4000
        CHECK(std::abs(magnitude - 0x4000) <= 40);
    }
}

TEST_CASE("geometry: square root and hypotenuse") {
    CHECK(square_root<uint32_t>(0) == 0);
    CHECK(square_root<uint32_t>(144) == 12);
    CHECK(square_root<uint32_t>(1000000) == 1000);
    CHECK(hypotenuse(3000, 4000) == 5000);
    // Rounded to the nearest whole root: 2.83 is 3 and 3.87 is 4, and a negative square has none.
    CHECK(square_root<int64_t>(8) == 3);
    CHECK(square_root<int64_t>(15) == 4);
    CHECK(square_root<int64_t>(4294967295LL) == 65536);
    CHECK(square_root<int32_t>(-4) == 0);
}
