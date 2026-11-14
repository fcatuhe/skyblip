// The exponential codes ADS-L G.1.6 packs its fields into, which the ALP-TAS frame shares with
// other widths: a code that rounds the wrong way reaches the pilot as a target at the wrong height
// or speed.
#include <cstdint>
#include <cstdlib>

#include "doctest/doctest.h"
#include "protocols/common/exponential.h"

using namespace skyblip::wire;

TEST_CASE("exponential: an unsigned value comes back within the step its code names") {
    const Exponential speed{6, 2, false};
    for (int32_t value = 0; value < 900; value += 7) {
        const int32_t decoded = speed.decode_middle(speed.encode(value));
        CHECK(std::abs(decoded - value) <= 8);
    }
    CHECK(speed.encode(5000) == speed.largest_magnitude_code());
    CHECK(speed.encode(-5) == 0);
}

TEST_CASE("exponential: a signed value keeps its sign and the step its magnitude names") {
    const Exponential climb{6, 2, true};
    for (int32_t value = -900; value < 900; value += 13) {
        const int32_t decoded = climb.decode_middle(climb.encode(value));
        CHECK(((value < 0) == (decoded < 0) || decoded == 0));
        CHECK(std::abs(std::abs(value) - std::abs(decoded)) <= 8);
    }
}

// One exponent bit, as an ALP-TAS altitude carries: 12 bits at 1 m, then 12 bits at 2 m.
TEST_CASE("exponential: a single exponent bit doubles the step once") {
    const Exponential altitude{12, 1, false};
    CHECK(altitude.encode(4095) == 4095u);
    CHECK(altitude.encode(4096) == 4096u);
    CHECK(altitude.encode(4097) == 4096u);
    CHECK(altitude.decode(4097) == 4098);
    CHECK(altitude.encode(1000000) == 8191u);
    CHECK(altitude.decode(8191) == 12286);
}
