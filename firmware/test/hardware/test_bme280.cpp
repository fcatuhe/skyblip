// The barometer triggered without waiting, read once converted, dated at its conversion.
#include <cstdlib>
#include <initializer_list>

#include "doctest/doctest.h"
#include "hardware/parts/bme280/bme280.h"
#include "hardware/parts/bme280/model.h"
#include "hardware/platform/host/clock.h"

using namespace skyblip;

namespace {

constexpr uint32_t kConvertedMs = parts::Bme280::kMeasureUs / 1000 + 1;

struct Bench {
    Bench() { chip.attach_clock(clock); }

    parts::Bme280::Reading convert() {
        REQUIRE(baro.trigger());
        clock.advance(kConvertedMs);
        parts::Bme280::Reading reading{};
        REQUIRE(baro.read(reading));
        return reading;
    }

    platform::host::Clock clock;
    models::Bme280 chip;
    parts::Bme280 baro{chip};
};

}  // namespace

TEST_CASE("bme280: bring-up sets the IIR filter to 4 and humidity off, and converts nothing") {
    Bench b;

    REQUIRE(b.baro.begin() == Status::Ok);

    CHECK(int(b.chip.mode()) == 0);
    CHECK(b.chip.conversions == 0);
    CHECK(b.chip.filter_coefficient() == 4);
    CHECK(b.chip.humidity_oversampling() == 0);
}

// The Zephyr fetch wrote ctrl_meas and slept out the conversion: 43 ms of the loop at every PPS.
TEST_CASE("bme280: a trigger starts one forced conversion and does not wait for it") {
    Bench b;
    REQUIRE(b.baro.begin() == Status::Ok);
    const int writes = b.chip.writes;
    const int reads = b.chip.reads;

    REQUIRE(b.baro.trigger());

    CHECK(b.chip.writes - writes == 1);
    CHECK(b.chip.reads == reads);
    CHECK(b.chip.conversions == 1);
    CHECK(b.chip.measuring());
    CHECK(b.chip.pressure_oversampling() == 16);
    CHECK(b.chip.temperature_oversampling() == 2);
}

TEST_CASE("bme280: a read before the conversion ends reports nothing and a later one gets it") {
    Bench b;
    b.chip.set_pressure_mpa(95000000);
    REQUIRE(b.baro.trigger());

    b.clock.advance(10);
    parts::Bme280::Reading reading{};
    CHECK_FALSE(b.baro.read(reading));
    CHECK(b.chip.result_reads == 0);

    b.clock.advance(kConvertedMs - 10);
    REQUIRE(b.baro.read(reading));
    CHECK(b.chip.result_reads == 1);
    CHECK(std::abs(int64_t{reading.pressure_mpa} - 95000000) <= 600);

    CHECK_FALSE(b.baro.read(reading));
    CHECK(b.chip.result_reads == 1);
}

TEST_CASE("bme280: a conversion is dated at the middle of its pressure phase") {
    // DS 9.1 typical: 1 + 2 x 2 ms of temperature, then half of 2 x 16 + 0.5 ms of pressure.
    CHECK(parts::Bme280::kMeasureUs == 37500);
    CHECK(parts::Bme280::kConversionMidpointMs == 21);
}

TEST_CASE(
    "bme280: the pressure read is the one the part was given, within six tenths of a pascal") {
    Bench b;

    for (int16_t deci : {int16_t{-200}, int16_t{150}, int16_t{452}}) {
        for (uint32_t pa = 30000; pa <= 110000; pa += 7919) {
            b.chip.set_temperature_decicelsius(deci);
            b.chip.set_pressure_mpa(pa * 1000 + 371);
            const parts::Bme280::Reading reading = b.convert();
            CHECK(reading.temperature_decicelsius == deci);
            const int64_t error = int64_t{reading.pressure_mpa} - (int64_t{pa} * 1000 + 371);
            // DS 4.2.3 and DS 8.1 part by up to 13 t_fine counts at -20 C, which is 0.45 Pa.
            CHECK(error <= 600);
            CHECK(error >= -600);
        }
    }
}

TEST_CASE("bme280: the air is the air at the trigger, not at the read") {
    Bench b;
    b.chip.set_pressure_mpa(95000000);
    REQUIRE(b.baro.trigger());
    b.chip.set_pressure_mpa(94000000);
    b.clock.advance(kConvertedMs);

    parts::Bme280::Reading reading{};
    REQUIRE(b.baro.read(reading));
    CHECK(std::abs(int64_t{reading.pressure_mpa} - 95000000) <= 600);
}

TEST_CASE("bme280: a part strapped to the alternate address is found there") {
    Bench b;
    b.chip.address = parts::Bme280::kAddressAlternate;

    CHECK(b.baro.answers());
    REQUIRE(b.baro.begin() == Status::Ok);
    CHECK(int(b.baro.address()) == 0x77);
    b.convert();
}

TEST_CASE("bme280: an address that names another chip is not a barometer") {
    Bench b;
    b.chip.chip_id = 0x58;
    b.chip.power_on_reset();

    CHECK_FALSE(b.baro.answers());
    CHECK(b.baro.begin() != Status::Ok);
    CHECK_FALSE(b.baro.trigger());
    CHECK(b.chip.conversions == 0);
}

TEST_CASE("bme280: a part that reset under us is configured again before its next trigger") {
    Bench b;
    b.convert();

    b.chip.power_on_reset();
    REQUIRE(b.baro.trigger());
    b.clock.advance(kConvertedMs);
    parts::Bme280::Reading reading{};
    CHECK_FALSE(b.baro.read(reading));
    CHECK(b.chip.filter_coefficient() == 1);

    b.chip.set_pressure_mpa(95000000);
    CHECK(std::abs(int64_t{b.convert().pressure_mpa} - 95000000) <= 600);
    CHECK(b.chip.filter_coefficient() == 4);
}

TEST_CASE("bme280: a read with no conversion in flight reports nothing and touches no bus") {
    Bench b;
    b.convert();
    const int reads = b.chip.reads;

    parts::Bme280::Reading reading{};
    CHECK_FALSE(b.baro.read(reading));
    CHECK(b.chip.reads == reads);
}

TEST_CASE("bme280: a part that stops answering is not read as its last pressure") {
    Bench b;
    b.convert();

    b.chip.answers = false;
    CHECK_FALSE(b.baro.trigger());
    parts::Bme280::Reading reading{};
    CHECK_FALSE(b.baro.read(reading));

    b.chip.answers = true;
    b.convert();
}
