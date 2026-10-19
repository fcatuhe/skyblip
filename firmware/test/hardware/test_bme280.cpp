// The barometer triggered without waiting, read once converted, dated at its conversion.
#include <cstdlib>
#include <initializer_list>

#include "doctest/doctest.h"
#include "hardware/parts/bme280/bme280.h"
#include "hardware/parts/bme280/model.h"
#include "hardware/platform/host/clock.h"

using namespace skyblip;

namespace {

constexpr uint32_t kConvertedMs = parts::Bme280::kMeasurementUs / 1000 + 1;

struct Bench {
    Bench() { chip.attach_clock(clock); }

    parts::Bme280::Reading convert() {
        REQUIRE(barometer.trigger());
        clock.advance(kConvertedMs);
        parts::Bme280::Reading reading{};
        REQUIRE(barometer.read(reading));
        return reading;
    }

    platform::host::Clock clock;
    models::Bme280 chip;
    parts::Bme280 barometer{chip};
};

}  // namespace

TEST_CASE("bme280: bring-up sets the IIR filter to 4 and humidity off, and converts nothing") {
    Bench bench;

    REQUIRE(bench.barometer.begin() == Status::Ok);

    CHECK(int(bench.chip.mode()) == 0);
    CHECK(bench.chip.conversions == 0);
    CHECK(bench.chip.filter_coefficient() == 4);
    CHECK(bench.chip.humidity_oversampling() == 0);
}

// The Zephyr fetch wrote ctrl_meas and slept out the conversion: 43 ms of the loop at every PPS.
TEST_CASE("bme280: a trigger starts one forced conversion and does not wait for it") {
    Bench bench;
    REQUIRE(bench.barometer.begin() == Status::Ok);
    const int writes = bench.chip.writes;
    const int reads = bench.chip.reads;

    REQUIRE(bench.barometer.trigger());

    CHECK(bench.chip.writes - writes == 1);
    CHECK(bench.chip.reads == reads);
    CHECK(bench.chip.conversions == 1);
    CHECK(bench.chip.measuring());
    CHECK(bench.chip.pressure_oversampling() == 16);
    CHECK(bench.chip.temperature_oversampling() == 2);
}

TEST_CASE("bme280: a read before the conversion ends reports nothing and a later one gets it") {
    Bench bench;
    bench.chip.set_pressure_mpa(95000000);
    REQUIRE(bench.barometer.trigger());

    bench.clock.advance(10);
    parts::Bme280::Reading reading{};
    CHECK_FALSE(bench.barometer.read(reading));
    CHECK(bench.chip.result_reads == 0);

    bench.clock.advance(kConvertedMs - 10);
    REQUIRE(bench.barometer.read(reading));
    CHECK(bench.chip.result_reads == 1);
    CHECK(std::abs(int64_t{reading.pressure_mpa} - 95000000) <= 600);

    CHECK_FALSE(bench.barometer.read(reading));
    CHECK(bench.chip.result_reads == 1);
}

TEST_CASE("bme280: a conversion is dated at the middle of its pressure phase") {
    // DS 9.1 typical: 1 + 2 x 2 ms of temperature, then half of 2 x 16 + 0.5 ms of pressure.
    CHECK(parts::Bme280::kMeasurementUs == 37500);
    CHECK(parts::Bme280::kConversionMidpointMs == 21);
}

TEST_CASE(
    "bme280: the pressure read is the one the part was given, within six tenths of a pascal") {
    Bench bench;

    for (int16_t temperature_dc : {int16_t{-200}, int16_t{150}, int16_t{452}}) {
        for (uint32_t pressure_pa = 30000; pressure_pa <= 110000; pressure_pa += 7919) {
            bench.chip.set_temperature_dc(temperature_dc);
            bench.chip.set_pressure_mpa(pressure_pa * 1000 + 371);
            const parts::Bme280::Reading reading = bench.convert();
            CHECK(reading.temperature_dc == temperature_dc);
            const int64_t error =
                int64_t{reading.pressure_mpa} - (int64_t{pressure_pa} * 1000 + 371);
            // DS 4.2.3 and 8.1 part by up to 13 fine_temperature counts at -20 C, which is 0.45 Pa.
            CHECK(error <= 600);
            CHECK(error >= -600);
        }
    }
}

TEST_CASE("bme280: the air is the air at the trigger, not at the read") {
    Bench bench;
    bench.chip.set_pressure_mpa(95000000);
    REQUIRE(bench.barometer.trigger());
    bench.chip.set_pressure_mpa(94000000);
    bench.clock.advance(kConvertedMs);

    parts::Bme280::Reading reading{};
    REQUIRE(bench.barometer.read(reading));
    CHECK(std::abs(int64_t{reading.pressure_mpa} - 95000000) <= 600);
}

TEST_CASE("bme280: a part strapped to the alternate address is found there") {
    Bench bench;
    bench.chip.bus_address = parts::Bme280::kAddressAlternate;

    CHECK(bench.barometer.answers());
    REQUIRE(bench.barometer.begin() == Status::Ok);
    CHECK(int(bench.barometer.address()) == 0x77);
    bench.convert();
}

TEST_CASE("bme280: an address that names another chip is not a barometer") {
    Bench bench;
    bench.chip.chip_id = 0x58;
    bench.chip.power_on_reset();

    CHECK_FALSE(bench.barometer.answers());
    CHECK(bench.barometer.begin() != Status::Ok);
    CHECK_FALSE(bench.barometer.trigger());
    CHECK(bench.chip.conversions == 0);
}

TEST_CASE("bme280: a part that reset under us is configured again before its next trigger") {
    Bench bench;
    bench.convert();

    bench.chip.power_on_reset();
    REQUIRE(bench.barometer.trigger());
    bench.clock.advance(kConvertedMs);
    parts::Bme280::Reading reading{};
    CHECK_FALSE(bench.barometer.read(reading));
    CHECK(bench.chip.filter_coefficient() == 1);

    bench.chip.set_pressure_mpa(95000000);
    CHECK(std::abs(int64_t{bench.convert().pressure_mpa} - 95000000) <= 600);
    CHECK(bench.chip.filter_coefficient() == 4);
}

TEST_CASE("bme280: a read with no conversion in flight reports nothing and touches no bus") {
    Bench bench;
    bench.convert();
    const int reads = bench.chip.reads;

    parts::Bme280::Reading reading{};
    CHECK_FALSE(bench.barometer.read(reading));
    CHECK(bench.chip.reads == reads);
}

TEST_CASE("bme280: a part that stops answering is not read as its last pressure") {
    Bench bench;
    bench.convert();

    bench.chip.answers = false;
    CHECK_FALSE(bench.barometer.trigger());
    parts::Bme280::Reading reading{};
    CHECK_FALSE(bench.barometer.read(reading));

    bench.chip.answers = true;
    bench.convert();
}
