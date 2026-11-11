// The X1's barometer commanded without waiting, compensated in integers, checked against DS 4.6.1.
#include <cmath>
#include <cstdlib>
#include <initializer_list>

#include "core/flight/atmosphere.h"
#include "doctest/doctest.h"
#include "hardware/parts/spa06/model.h"
#include "hardware/parts/spa06/spa06.h"
#include "hardware/platform/host/clock.h"

using namespace skyblip;

namespace {

constexpr uint32_t kPressureMeasuredMs = parts::Spa06::kPressureMeasurementUs / 1000 + 1;
constexpr uint32_t kTemperatureMeasuredMs = parts::Spa06::kTemperatureMeasurementUs / 1000 + 1;
// Half a raw count where the fixture is steepest, 300 hPa: 0.073 Pa a count.
constexpr int64_t kToleranceMpa = 37;

struct Bench {
    Bench() { chip.attach_clock(clock); }

    parts::Spa06::Reading convert() {
        REQUIRE(barometer.trigger());
        clock.advance(kPressureMeasuredMs);
        parts::Spa06::Reading reading{};
        REQUIRE_FALSE(barometer.read(reading));
        clock.advance(kTemperatureMeasuredMs);
        REQUIRE(barometer.read(reading));
        return reading;
    }

    platform::host::Clock clock;
    models::Spa06 chip;
    parts::Spa06 barometer{chip};
};

// An invented set, not a unit's: every sign flipped but c00's, as a negative offset is not air.
constexpr models::Spa06::Coefficients kOtherSigns{-100, 300,  120000, 70000, 9000, 1300,
                                                  -10,  2600, -1100,  -140,  -25};

int64_t pressure_error_mpa(const parts::Spa06::Reading& reading, uint32_t pressure_mpa) {
    return int64_t{reading.pressure_mpa} - int64_t{pressure_mpa};
}

}  // namespace

TEST_CASE(
    "spa06: bring-up sets 64x pressure with its shift and single temperature, and measures "
    "nothing") {
    Bench bench;

    REQUIRE(bench.barometer.begin() == Status::Ok);

    CHECK(bench.chip.pressure_oversampling() == 64);
    CHECK(bench.chip.pressure_shifted());
    CHECK(bench.chip.config() == 0x04);
    CHECK(bench.chip.temperature_oversampling() == 1);
    CHECK(bench.chip.pressure_commands == 0);
    CHECK(bench.chip.temperature_commands == 0);
}

TEST_CASE("spa06: bring-up stops the background measurements another firmware left running") {
    Bench bench;
    const uint8_t continuous_both[] = {models::Spa06::kRegisterMeasurementConfig, 0b111};
    REQUIRE(bench.chip.write(models::Spa06::kAddress, continuous_both, 2));

    REQUIRE(bench.barometer.begin() == Status::Ok);

    CHECK(int(bench.chip.measurement_control()) == 0);
}

TEST_CASE("spa06: a part still loading its coefficients is not brought up until it has") {
    Bench bench;
    bench.chip.starting = true;

    CHECK(bench.barometer.begin() == Status::WouldBlock);
    CHECK_FALSE(bench.barometer.trigger());
    CHECK(bench.chip.pressure_commands == 0);

    bench.chip.starting = false;
    CHECK(std::abs(pressure_error_mpa(bench.convert(), flight::kIsaSeaLevelPa * 1000)) <=
          kToleranceMpa);
}

// A command-mode measurement is one type at a time, so the pressure goes first and its temperature
// follows.
TEST_CASE("spa06: a trigger commands one pressure measurement and does not wait for it") {
    Bench bench;
    REQUIRE(bench.barometer.begin() == Status::Ok);

    REQUIRE(bench.barometer.trigger());

    CHECK(bench.chip.pressure_commands == 1);
    CHECK(bench.chip.temperature_commands == 0);
    CHECK(bench.chip.measuring());
}

TEST_CASE(
    "spa06: the temperature is measured once the pressure is in, and the reading waits for "
    "both") {
    Bench bench;
    bench.chip.set_pressure_mpa(95000000);
    REQUIRE(bench.barometer.trigger());
    parts::Spa06::Reading reading{};

    bench.clock.advance(10);
    CHECK_FALSE(bench.barometer.read(reading));
    CHECK(bench.chip.temperature_commands == 0);

    bench.clock.advance(kPressureMeasuredMs - 10);
    CHECK_FALSE(bench.barometer.read(reading));
    CHECK(bench.chip.temperature_commands == 1);
    CHECK_FALSE(bench.barometer.read(reading));

    bench.clock.advance(kTemperatureMeasuredMs);
    REQUIRE(bench.barometer.read(reading));
    CHECK(std::abs(pressure_error_mpa(reading, 95000000)) <= kToleranceMpa);
    CHECK(reading.temperature_dc == flight::kIsaSeaLevelDeciCelsius);

    CHECK_FALSE(bench.barometer.read(reading));
    CHECK(bench.chip.pressure_commands == 1);
    CHECK(bench.chip.temperature_commands == 1);
}

TEST_CASE("spa06: a conversion is dated at the middle of its pressure measurement") {
    // DS table 8: 104.4 ms at 64 times, half of it after the command.
    CHECK(parts::Spa06::kPressureMeasurementUs == 104400);
    CHECK(parts::Spa06::kConversionMidpointMs == 52);
}

TEST_CASE("spa06: the pressure read is the one the part was given, within half a raw count") {
    Bench bench;

    for (int16_t temperature_dc : {int16_t{-200}, int16_t{150}, int16_t{452}}) {
        for (uint32_t pressure_pa = 30000; pressure_pa <= 110000; pressure_pa += 7919) {
            const uint32_t pressure_mpa = pressure_pa * 1000 + 371;
            bench.chip.set_temperature_dc(temperature_dc);
            bench.chip.set_pressure_mpa(pressure_mpa);
            const parts::Spa06::Reading reading = bench.convert();
            CHECK(reading.temperature_dc == temperature_dc);
            CHECK(std::abs(pressure_error_mpa(reading, pressure_mpa)) <= kToleranceMpa);
        }
    }
}

// DS table 10 packs fields of 12, 16 and 20 bits, and each is sign-extended from its own top bit.
TEST_CASE("spa06: a unit whose coefficients carry the other signs is read as truly") {
    Bench bench;
    bench.chip.coefficients = kOtherSigns;
    bench.chip.power_on_reset();

    for (int16_t temperature_dc : {int16_t{-200}, int16_t{452}}) {
        for (uint32_t pressure_pa : {30000u, 101325u, 110000u}) {
            bench.chip.set_temperature_dc(temperature_dc);
            bench.chip.set_pressure_mpa(pressure_pa * 1000);
            const parts::Spa06::Reading reading = bench.convert();
            CHECK(reading.temperature_dc == temperature_dc);
            CHECK(std::abs(pressure_error_mpa(reading, pressure_pa * 1000)) <= kToleranceMpa);
        }
    }
}

// Invented sets that stay air with raw words at their 24-bit ends. The second is a review's case.
constexpr models::Spa06::Coefficients kSmallTerms[] = {{50, 0, 100000, 3, -5, 7, 1, -3, 5, -7, 1},
                                                       {50, 0, 100000, 0, 0, 0, 1, 0, 0, 0, 0}};

TEST_CASE(
    "spa06: the integer compensation stays within the README's bound of DS 4.6.1 over every "
    "raw word") {
    const int32_t raw_pressures[] = {-(1 << 23), -7000001, -1040384,     0,
                                     1040384,    7000001,  (1 << 23) - 1};
    const int32_t raw_temperatures[] = {-(1 << 23), -524288, 0, 524288, (1 << 23) - 1};
    for (const models::Spa06::Coefficients& coefficients : kSmallTerms) {
        for (int32_t raw_pressure : raw_pressures) {
            for (int32_t raw_temperature : raw_temperatures) {
                Bench bench;
                bench.chip.coefficients = coefficients;
                bench.chip.power_on_reset();
                bench.chip.raw_forced = true;
                bench.chip.forced_raw_pressure = raw_pressure;
                bench.chip.forced_raw_temperature = raw_temperature;
                const parts::Spa06::Reading reading = bench.convert();

                const double x = std::fabs(raw_pressure / 1040384.0);
                const double t = std::fabs(raw_temperature / 524288.0);
                const double half_steps =
                    0.5 * (1 + x + x * x + x * x * x) + 0.5 * (1 + x + x * x) * t + 0.5;
                const double bound_mpa = 0.5 + half_steps * 1000 / 65536;
                const double want_mpa = bench.chip.pascals(raw_pressure, raw_temperature) * 1000;
                CHECK(std::fabs(reading.pressure_mpa - want_mpa) <= bound_mpa);
                CHECK(bound_mpa < 14.2);
            }
        }
    }
}

TEST_CASE("spa06: a reading is the altitude the part was carried to, on the BME280's path") {
    Bench bench;

    for (int32_t altitude_mm : {-300000, 0, 1500000, 6000000}) {
        bench.chip.set_altitude_mm(altitude_mm);
        const int32_t read_mm = flight::pressure_to_altitude_mm(bench.convert().pressure_mpa);
        // 37 mPa is under 1 cm at 6 km, and the table's own inverse rounds to the millimetre.
        CHECK(std::abs(read_mm - altitude_mm) <= 10);
    }
}

TEST_CASE("spa06: the air is the air at the pressure command, not at the read") {
    Bench bench;
    bench.chip.set_pressure_mpa(95000000);
    REQUIRE(bench.barometer.trigger());
    bench.chip.set_pressure_mpa(94000000);
    bench.clock.advance(kPressureMeasuredMs);

    parts::Spa06::Reading reading{};
    CHECK_FALSE(bench.barometer.read(reading));
    bench.clock.advance(kTemperatureMeasuredMs);
    REQUIRE(bench.barometer.read(reading));
    CHECK(std::abs(pressure_error_mpa(reading, 95000000)) <= kToleranceMpa);
}

TEST_CASE("spa06: a trigger reads away the result an unread conversion left ready") {
    Bench bench;
    bench.chip.set_pressure_mpa(95000000);
    REQUIRE(bench.barometer.trigger());
    bench.clock.advance(250);

    bench.chip.set_pressure_mpa(94000000);
    REQUIRE(bench.barometer.trigger());
    parts::Spa06::Reading reading{};
    CHECK_FALSE(bench.barometer.read(reading));
    CHECK(bench.chip.temperature_commands == 0);

    bench.clock.advance(kPressureMeasuredMs);
    CHECK_FALSE(bench.barometer.read(reading));
    bench.clock.advance(kTemperatureMeasuredMs);
    REQUIRE(bench.barometer.read(reading));
    CHECK(std::abs(pressure_error_mpa(reading, 94000000)) <= kToleranceMpa);
}

TEST_CASE("spa06: a part strapped to the alternate address is found there") {
    Bench bench;
    bench.chip.bus_address = parts::Spa06::kAddressAlternate;

    CHECK(bench.barometer.answers());
    REQUIRE(bench.barometer.begin() == Status::Ok);
    CHECK(int(bench.barometer.address()) == 0x76);
    bench.convert();
}

TEST_CASE("spa06: an address that names another chip is not a barometer") {
    Bench bench;
    bench.chip.chip_id = 0x10;
    bench.chip.power_on_reset();

    CHECK_FALSE(bench.barometer.answers());
    CHECK(bench.barometer.begin() != Status::Ok);
    CHECK_FALSE(bench.barometer.trigger());
    CHECK(bench.chip.pressure_commands == 0);
}

TEST_CASE("spa06: a part that reset under us is configured again before its next trigger") {
    Bench bench;
    bench.convert();

    bench.chip.power_on_reset();
    REQUIRE(bench.barometer.trigger());
    bench.clock.advance(kPressureMeasuredMs);
    parts::Spa06::Reading reading{};
    CHECK_FALSE(bench.barometer.read(reading));
    CHECK(bench.chip.pressure_oversampling() == 1);
    CHECK_FALSE(bench.chip.pressure_shifted());

    bench.chip.set_pressure_mpa(95000000);
    CHECK(std::abs(pressure_error_mpa(bench.convert(), 95000000)) <= kToleranceMpa);
    CHECK(bench.chip.pressure_oversampling() == 64);
    CHECK(bench.chip.pressure_shifted());
}

TEST_CASE("spa06: a read with no conversion in flight reports nothing and touches no bus") {
    Bench bench;
    bench.convert();
    const int reads = bench.chip.reads;

    parts::Spa06::Reading reading{};
    CHECK_FALSE(bench.barometer.read(reading));
    CHECK(bench.chip.reads == reads);
}

TEST_CASE("spa06: a part that stops answering is not read as its last pressure") {
    Bench bench;
    bench.convert();

    bench.chip.answers = false;
    CHECK_FALSE(bench.barometer.trigger());
    parts::Spa06::Reading reading{};
    CHECK_FALSE(bench.barometer.read(reading));

    bench.chip.answers = true;
    bench.convert();
}

// No air is at 4000 kPa, so the model pins the raw word at the end its fixture slopes to.
TEST_CASE("spa06: a raw word at either end of its range is refused, not overflowed") {
    for (const models::Spa06::Coefficients& coefficients : {models::Spa06::kFixture, kOtherSigns}) {
        Bench bench;
        bench.chip.coefficients = coefficients;
        bench.chip.power_on_reset();
        bench.chip.set_pressure_mpa(4000000000u);

        REQUIRE(bench.barometer.trigger());
        bench.clock.advance(kPressureMeasuredMs);
        parts::Spa06::Reading reading{};
        CHECK_FALSE(bench.barometer.read(reading));
        bench.clock.advance(kTemperatureMeasuredMs);
        CHECK_FALSE(bench.barometer.read(reading));
        CHECK(bench.chip.temperature_commands == 1);
    }
}

TEST_CASE("spa06: a temperature the part cannot be at is not a reading") {
    Bench bench;
    bench.chip.set_temperature_dc(900);

    REQUIRE(bench.barometer.trigger());
    bench.clock.advance(kPressureMeasuredMs);
    parts::Spa06::Reading reading{};
    CHECK_FALSE(bench.barometer.read(reading));
    bench.clock.advance(kTemperatureMeasuredMs);
    CHECK_FALSE(bench.barometer.read(reading));
}
