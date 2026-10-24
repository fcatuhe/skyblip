// The integer arithmetic the rest of the firmware trusts without checking: fixed
// point instead of floats, a CORDIC sine, an integer square root, the variable
// resolution codecs ADS-L packs its fields into. Every one is a silent failure
// mode. A variable-range codec that rounds the wrong way or an arctangent that is a quadrant out
// reaches the pilot as a target in the wrong place, with nothing logged.
#include <cmath>
#include <cstdlib>  // std::abs
#include <cstring>
#include <string>

#include "core/units/units.h"
#include "core/util/fifo.h"
#include "core/util/format.h"
#include "core/util/intmath.h"
#include "core/util/result.h"
#include "core/util/variable_range.h"
#include "doctest/doctest.h"

using namespace skyblip;

TEST_CASE("units: feet<->metres round-trip is close") {
    for (int feet = -1000; feet <= 40000; feet += 137) {
        Metres metres = to_metres(Feet(feet));
        Feet back = to_feet(metres);
        // integer double-conversion truncation. A few feet is physically moot
        CHECK(std::abs(back.value - feet) <= 5 + std::abs(feet) / 6000);
    }
}

// Every one truncated, so a speed, a climb and a rate each read a unit below what was flown.
TEST_CASE("units: a speed and a rate are rounded to the unit shown, not truncated") {
    // 60 kt is 30.87 m/s, which the wire carries as 123 quarter-m/s: 59.77 kt back.
    CHECK(to_knots(QuarterMetresPerSecond(123)).value == 60);
    CHECK(to_kmh(QuarterMetresPerSecond(123)).value == 111);
    CHECK(to_knots(QuarterMetresPerSecond(0)).value == 0);

    // 300 fpm is 1.524 m/s.
    CHECK(to_feet_per_minute(MillimetresPerSecond(1524)).value == 300);
    CHECK(to_feet_per_minute(MillimetresPerSecond(-1524)).value == -300);
    CHECK(to_feet_per_minute(MillimetresPerSecond(0)).value == 0);
}

TEST_CASE("units: cordic9 to degrees") {
    CHECK(to_degrees(Cordic9(0)).value == 0);
    CHECK(to_degrees(Cordic9(128)).value == 90);
    CHECK(to_degrees(Cordic9(256)).value == 180);
    CHECK(to_degrees(Cordic9(384)).value == 270);
}

TEST_CASE("result: ok and error carry status") {
    Result<int> good(42);
    CHECK(good.ok());
    CHECK(good.value() == 42);
    Result<int> bad(Status::Invalid);
    CHECK_FALSE(bad.ok());
    CHECK(bad.status() == Status::Invalid);
    CHECK(bad.value_or(-1) == -1);
}

TEST_CASE("fifo: bounded ring behaviour") {
    Fifo<int, 4> fifo;  // capacity 3
    CHECK(fifo.empty());
    CHECK(fifo.push(1) == Status::Ok);
    CHECK(fifo.push(2) == Status::Ok);
    CHECK(fifo.push(3) == Status::Ok);
    CHECK(fifo.full());
    CHECK(fifo.push(4) == Status::Full);
    CHECK(fifo.pop().value() == 1);
    CHECK(fifo.pop().value() == 2);
    CHECK(fifo.push(5) == Status::Ok);
    CHECK(fifo.pop().value() == 3);
    CHECK(fifo.pop().value() == 5);
    CHECK(fifo.pop().status() == Status::Empty);
}

TEST_CASE("variable range: unsigned encode/decode round-trip (within representable range)") {
    // Bits=6 range encoding is representable up to ~956. Below that the coarsest
    // quantization step is 8.
    for (uint16_t value = 0; value < 900; value += 7) {
        uint16_t encoded = encode_unsigned_variable_range<uint16_t, 6>(value);
        uint16_t decoded = decode_unsigned_variable_range<uint16_t, 6>(encoded);
        CHECK(std::abs(int(decoded) - int(value)) <= 8);  // within the coarsest step
    }
    // Saturation: large values clamp to the max code, not garbage.
    CHECK(decode_unsigned_variable_range<uint16_t, 6>(
              encode_unsigned_variable_range<uint16_t, 6>(5000)) <= 960);
}

TEST_CASE("variable range: signed preserves sign and magnitude bucket") {
    for (int16_t value = -900; value < 900; value += 13) {
        int16_t encoded = encode_signed_variable_range<int16_t, 6>(value);
        int16_t decoded = decode_signed_variable_range<int16_t, 6>(encoded);
        bool sign_ok = ((value < 0) == (decoded < 0)) || (decoded == 0);
        CHECK(sign_ok);
        CHECK(std::abs(std::abs(value) - std::abs(decoded)) <= 8);
    }
}

TEST_CASE("intmath: sine/cosine magnitude and quadrature") {
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

TEST_CASE("intmath: square root and hypotenuse") {
    CHECK(square_root<uint32_t>(0) == 0);
    CHECK(square_root<uint32_t>(144) == 12);
    CHECK(square_root<uint32_t>(1000000) == 1000);
    CHECK(hypotenuse(3000, 4000) == 5000);
}

TEST_CASE("intmath: arctangent cardinal directions (16-bit cyclic)") {
    auto to_degrees = [](int16_t angle) { return (int(uint16_t(angle)) * 360) / 65536; };
    CHECK(to_degrees(arctangent(0, 1000)) == 0);
    CHECK(std::abs(to_degrees(arctangent(1000, 0)) - 90) <= 1);
    CHECK(std::abs(to_degrees(arctangent(0, -1000)) - 180) <= 1);
}

// The cardinals were exact while everything between them was up to 8 degrees out.
TEST_CASE("intmath: arctangent holds a degree everywhere, not only on the cardinals") {
    double worst = 0;
    for (int degrees = 0; degrees < 360; degrees++) {
        const double radians = degrees * M_PI / 180.0;
        const int16_t angle =
            arctangent(static_cast<int32_t>(std::lround(10000 * std::sin(radians))),
                       static_cast<int32_t>(std::lround(10000 * std::cos(radians))));
        const double got = static_cast<double>(angle) * 360.0 / 65536.0;
        const double want = degrees > 180 ? degrees - 360 : degrees;
        worst = std::fmax(worst, degrees == 180 ? 0 : std::fabs(got - want));
    }
    CHECK(worst < 1.0);
}

TEST_CASE("format: hex, dec, fixed point") {
    char text[32];
    int written = format_hex(text, 0xABCD, 4);
    CHECK(std::string(text, written) == "ABCD");
    written = format_unsigned(text, 42);
    CHECK(std::string(text, written) == "42");
    written = format_unsigned(text, 5, 3);
    CHECK(std::string(text, written) == "005");
    written = format_signed(text, -17);
    CHECK(std::string(text, written) == "-17");
    written = format_unsigned(text, 1234, 1, 1);  // one decimal
    CHECK(std::string(text, written) == "123.4");
}

TEST_CASE("format: nmea lat/lon") {
    char buffer[32];
    int written = format_nmea_latitude(buffer, 481234500);  // 48.12345 deg
    std::string text(buffer, written);
    CHECK(text.substr(0, 2) == "48");
    CHECK(text.back() == 'N');
    written = format_nmea_longitude(buffer, -81234500);
    text.assign(buffer, written);
    CHECK(text.substr(0, 3) == "008");
    CHECK(text.back() == 'W');
}
