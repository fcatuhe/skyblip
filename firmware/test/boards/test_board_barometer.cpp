// When the board reads the barometer: that interval is what a vertical speed is taken over.
#include <vector>

#include "boards/lilygo/t_echo_plus/board.h"
#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "core/events/sensor.h"
#include "doctest/doctest.h"
#include "hardware/parts/bme280/bme280.h"
#include "hardware/platform/host/platform.h"
#include "runtime/tasks.h"

using namespace skyblip;

namespace {

using Board = boards::TEchoPlus<platform::host::Platform>;

constexpr uint32_t kMidpointMs = parts::Bme280::kConversionMidpointMs;

struct Unit {
    Unit() { REQUIRE(board.begin() == Status::Ok); }

    std::vector<uint32_t> sample_instants(uint32_t from_ms, uint32_t to_ms) {
        std::vector<uint32_t> at;
        for (uint32_t now_ms = from_ms; now_ms < to_ms; now_ms += runtime::kServicePassCeilingMs) {
            events::BarometerSample sample{};
            if (pass(now_ms, sample)) at.push_back(sample.at_ms);
        }
        return at;
    }

    std::vector<uint32_t> trigger_instants(uint32_t from_ms, uint32_t to_ms) {
        std::vector<uint32_t> at;
        for (uint32_t now_ms = from_ms; now_ms < to_ms; now_ms += runtime::kServicePassCeilingMs) {
            const int before = chip().conversions;
            events::BarometerSample sample{};
            pass(now_ms, sample);
            if (chip().conversions != before) at.push_back(now_ms);
        }
        return at;
    }

    bool pass(uint32_t now_ms, events::BarometerSample& sample) {
        platform.clock().set_milliseconds(now_ms);
        board.poll(state, now_ms);
        return bus.barometer.pop(sample);
    }

    models::Bme280& chip() { return platform.barometer().chip; }

    platform::host::Platform platform;
    bus::Bus bus;
    Board board{platform, bus};
    bus::State state;
};

}  // namespace

// The Zephyr fetch slept 43 ms in this pass; the part now converts while the loop runs on.
TEST_CASE("board: a trigger never waits, and the edge's conversion is read on a later pass") {
    Unit unit;
    events::BarometerSample sample{};
    unit.sample_instants(0, 1000);
    const int conversions = unit.chip().conversions;
    const int reads = unit.chip().reads;

    CHECK_FALSE(unit.pass(1000, sample));
    CHECK(unit.chip().conversions == conversions + 1);
    CHECK(unit.chip().reads == reads);
    CHECK(unit.chip().measuring());
    CHECK_FALSE(unit.pass(1030, sample));
    CHECK(unit.pass(1040, sample));

    const int read_after_edge = unit.chip().reads;
    CHECK_FALSE(unit.pass(1250, sample));
    CHECK(unit.chip().conversions == conversions + 2);
    CHECK(unit.chip().reads == read_after_edge);
}

TEST_CASE("board: four conversions a second, one published") {
    Unit unit;
    unit.sample_instants(0, 1000);
    const int conversions = unit.chip().conversions;

    const std::vector<uint32_t> at = unit.sample_instants(1000, 11000);

    CHECK(unit.chip().conversions - conversions == 40);
    CHECK(at.size() == 10);
    CHECK(unit.board.barometer_faults() == 0);
}

TEST_CASE("board: triggers stay on the PPS phases") {
    Unit unit;

    const std::vector<uint32_t> at = unit.trigger_instants(1000, 4000);

    REQUIRE(at.size() == 12);
    for (size_t i = 0; i < at.size(); i++)
        CHECK(at[i] == 1000 + i * runtime::kBarometerConversionPeriodMs);
}

TEST_CASE("board: a barometer sample is dated at its conversion, not at its read") {
    Unit unit;

    const std::vector<uint32_t> at = unit.sample_instants(1000, 1100);

    REQUIRE(at.size() == 1);
    CHECK(at.front() == 1000 + kMidpointMs);
}

TEST_CASE("board: the published sample is the one triggered at the edge") {
    Unit unit;
    std::vector<uint32_t> published;
    for (uint32_t now_ms = 0; now_ms < 4000; now_ms += runtime::kServicePassCeilingMs) {
        unit.chip().set_pressure_mpa(now_ms % 1000 == 0 ? 95000000 : 90000000);
        events::BarometerSample sample{};
        if (unit.pass(now_ms, sample)) published.push_back(sample.pressure_mpa);
    }

    REQUIRE(published.size() == 3);
    for (uint32_t pressure_mpa : published) CHECK((pressure_mpa + 500) / 1000 == 95000);
}

TEST_CASE("board: the barometer is published once a second, on the PPS edge") {
    Unit unit;

    const std::vector<uint32_t> at = unit.sample_instants(0, 10000);

    REQUIRE(at.size() == 9);
    CHECK(at.front() == 1000 + kMidpointMs);
    for (size_t i = 0; i < at.size(); i++) {
        CHECK((at[i] - kMidpointMs) % 1000 < runtime::kBarometerPpsWindowMs);
        if (i > 0) CHECK(at[i] - at[i - 1] == runtime::kBarometerPublishPeriodMs);
    }
}

TEST_CASE("board: with no PPS the barometer keeps its own second and its own quarters") {
    Unit unit;
    unit.platform.pps().set_locked(false);

    const std::vector<uint32_t> at = unit.sample_instants(55, 10055);
    const std::vector<uint32_t> triggers = unit.trigger_instants(10055, 12055);

    REQUIRE(at.size() >= 9);
    for (size_t i = 1; i < at.size(); i++)
        CHECK(at[i] - at[i - 1] == runtime::kBarometerPublishPeriodMs);
    REQUIRE(triggers.size() == 8);
    for (size_t i = 1; i < triggers.size(); i++)
        CHECK(triggers[i] - triggers[i - 1] == runtime::kBarometerConversionPeriodMs);
}

TEST_CASE("board: a conversion that never ends is given up at its ceiling and counted") {
    Unit unit;
    unit.sample_instants(0, 1000);
    unit.chip().extra_measurement_us = 10000000;

    CHECK(unit.sample_instants(1000, 1000 + runtime::kBarometerConversionCeilingMs).empty());
    CHECK(unit.board.barometer_faults() == 0);
    CHECK(unit.sample_instants(1000 + runtime::kBarometerConversionCeilingMs, 1500).empty());
    CHECK(unit.board.barometer_faults() == 1);

    unit.chip().extra_measurement_us = 0;
    CHECK(unit.sample_instants(1500, 2100).size() == 1);
    CHECK(unit.board.barometer_faults() == 1);
}

TEST_CASE("board: a sample the sensor refused is not a sample of the last pressure") {
    Unit unit;
    unit.platform.barometer().fitted = false;

    CHECK(unit.sample_instants(1000, 5000).empty());
    CHECK(unit.board.barometer_faults() == 16);
}
