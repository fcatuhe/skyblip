// Standard-atmosphere tests. The oracle is the ICAO formula itself, evaluated
// independently here in floating point: the shipping code is an integer table,
// so agreeing with the closed form is a real check, not a restatement.
#include <cmath>

#include "core/flight/atmosphere.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

// h = 44330.77 * (1 - (p/101325)^0.190263), in centimetres.
double isa_altitude_cm(double pressure_pa) {
    return 44330.77 * (1.0 - std::pow(pressure_pa / 101325.0, 0.190263)) * 100.0;
}

}  // namespace

TEST_CASE("atmosphere: the table agrees with the ICAO closed form") {
    // 25 cm is the worst linear-interpolation error between table entries.
    for (uint32_t pressure_pa = 26000; pressure_pa <= 110000;
         pressure_pa += 137) {  // 137: a prime, so it lands off-grid
        const int32_t got = flight::pressure_to_altitude_cm(pressure_pa);
        CHECK(std::abs(got - isa_altitude_cm(pressure_pa)) < 25.0);
    }
}

TEST_CASE("atmosphere: sea level is zero and the curve descends with pressure") {
    // Approx(0) would be a relative test against zero. 25 cm is the table's bound.
    CHECK(std::abs(flight::pressure_to_altitude_cm(flight::kIsaSeaLevelPa)) < 25);

    int32_t previous = flight::pressure_to_altitude_cm(26000);
    for (uint32_t pressure_pa = 26500; pressure_pa <= 110000; pressure_pa += 500) {
        const int32_t altitude = flight::pressure_to_altitude_cm(pressure_pa);
        CHECK(altitude < previous);  // strictly monotonic: no plateau to hide a bad entry
        previous = altitude;
    }
}

TEST_CASE("atmosphere: out-of-range pressure clamps instead of wrapping") {
    CHECK(flight::pressure_to_altitude_cm(0) == flight::pressure_to_altitude_cm(26000));
    CHECK(flight::pressure_to_altitude_cm(1) == flight::pressure_to_altitude_cm(20000));
    CHECK(flight::pressure_to_altitude_cm(500000) == flight::pressure_to_altitude_cm(110000));
}

TEST_CASE("atmosphere: the inverse round-trips through the forward curve") {
    for (int32_t altitude_cm = -50000; altitude_cm <= 900000; altitude_cm += 4321) {
        const uint32_t pressure_pa = flight::altitude_cm_to_pressure_pa(altitude_cm);
        // Bisection lands within one pascal, which is well under a metre.
        CHECK(std::abs(flight::pressure_to_altitude_cm(pressure_pa) - altitude_cm) < 100);
    }
}

TEST_CASE("atmosphere: climb rate in mm/s, both signs") {
    int32_t climb_mm_s = 0;
    // +10 m over 2 s = +5 m/s.
    REQUIRE(flight::climb_mm_s_from_altitude(1010000, 1000000, 2000, climb_mm_s));
    CHECK(climb_mm_s == 5000);
    // Sinking at the same rate.
    REQUIRE(flight::climb_mm_s_from_altitude(1000000, 1010000, 2000, climb_mm_s));
    CHECK(climb_mm_s == -5000);
    // Level flight.
    REQUIRE(flight::climb_mm_s_from_altitude(1000000, 1000000, 2000, climb_mm_s));
    CHECK(climb_mm_s == 0);
}

TEST_CASE("atmosphere: a rate finer than the ADS-L unit survives the measurement") {
    // 12 cm in a second is a fifth of the 0.125 m/s ADS-L transmits in.
    int32_t climb_mm_s = 0;
    REQUIRE(flight::climb_mm_s_from_altitude(1000120, 1000000, 1000, climb_mm_s));
    CHECK(climb_mm_s == 120);
    CHECK(flight::climb_e8_from_mm_s(climb_mm_s) == 1);
    CHECK(flight::climb_e8_from_mm_s(-climb_mm_s) == -1);
    CHECK(flight::climb_e8_from_mm_s(5000) == 40);
    CHECK(flight::climb_e8_from_mm_s(0) == 0);
}

TEST_CASE("atmosphere: an unusable window is refused, not guessed at") {
    int32_t climb_mm_s = 123;
    CHECK_FALSE(flight::climb_mm_s_from_altitude(1010000, 1000000, 0, climb_mm_s));
    CHECK_FALSE(flight::climb_mm_s_from_altitude(1010000, 1000000, 100, climb_mm_s));
    CHECK_FALSE(flight::climb_mm_s_from_altitude(1010000, 1000000, 60000, climb_mm_s));
    CHECK(climb_mm_s == 123);  // the caller's value is left alone
}

TEST_CASE("atmosphere: an absurd rate saturates instead of overflowing int16") {
    int32_t climb_mm_s = 0;
    REQUIRE(flight::climb_mm_s_from_altitude(9000000, -500000, 500, climb_mm_s));
    CHECK(flight::climb_e8_from_mm_s(climb_mm_s) == 32767);
    REQUIRE(flight::climb_mm_s_from_altitude(-500000, 9000000, 500, climb_mm_s));
    CHECK(flight::climb_e8_from_mm_s(climb_mm_s) == -32768);
}

TEST_CASE("atmosphere: a real climb through the table reads back as its rate") {
    // 1000 m to 1010 m in 2 s is +5 m/s, computed only from pressures.
    const uint32_t pressure_low_mpa = flight::altitude_mm_to_pressure_mpa(1000000);
    const uint32_t pressure_high_mpa = flight::altitude_mm_to_pressure_mpa(1010000);
    int32_t climb_mm_s = 0;
    REQUIRE(flight::climb_mm_s_from_altitude(flight::pressure_to_altitude_mm(pressure_high_mpa),
                                             flight::pressure_to_altitude_mm(pressure_low_mpa),
                                             2000, climb_mm_s));
    CHECK(climb_mm_s == doctest::Approx(5000).epsilon(0.005));
}

TEST_CASE("atmosphere: a pascal is centimetres of altitude, so the curve is walked finer") {
    const int32_t sea = flight::pressure_to_altitude_mm(flight::kIsaSeaLevelPa * 1000);
    // 8.31 cm a pascal at sea level, and the interpolation rounds to the millimetre.
    CHECK(flight::pressure_to_altitude_mm((flight::kIsaSeaLevelPa - 1) * 1000) - sea == 84);
    CHECK(sea - flight::pressure_to_altitude_mm(flight::kIsaSeaLevelPa * 1000 + 1000) == 83);
}

TEST_CASE("atmosphere: the ISA temperature falls 6.5 C every kilometre from 15 C at the sea") {
    CHECK(flight::isa_temperature_dc(0) == flight::kIsaSeaLevelDeciCelsius);
    CHECK(flight::isa_temperature_dc(1000000) == 85);
    CHECK(flight::isa_temperature_dc(3000000) == -45);
    // Below the sea the air is warmer, and a 500 m mine shaft is 3.25 C of it.
    CHECK(flight::isa_temperature_dc(-500000) == 183);
}
