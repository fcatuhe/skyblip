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
        for (uint32_t t = from_ms; t < to_ms; t += runtime::kServiceStepMs) {
            events::BaroSample sample{};
            if (pass(t, sample)) at.push_back(sample.at_ms);
        }
        return at;
    }

    std::vector<uint32_t> trigger_instants(uint32_t from_ms, uint32_t to_ms) {
        std::vector<uint32_t> at;
        for (uint32_t t = from_ms; t < to_ms; t += runtime::kServiceStepMs) {
            const int before = chip().conversions;
            events::BaroSample sample{};
            pass(t, sample);
            if (chip().conversions != before) at.push_back(t);
        }
        return at;
    }

    bool pass(uint32_t t, events::BaroSample& sample) {
        platform.clock().set_millis(t);
        board.poll(state, t);
        return bus.baro.pop(sample);
    }

    models::Bme280& chip() { return platform.baro().chip; }

    platform::host::Platform platform;
    bus::Bus bus;
    Board board{platform, bus};
    bus::State state;
};

}  // namespace

// The Zephyr fetch slept 43 ms in this pass; the part now converts while the loop runs on.
TEST_CASE("board: a trigger never waits, and the edge's conversion is read on a later pass") {
    Unit u;
    events::BaroSample sample{};
    u.sample_instants(0, 1000);
    const int conversions = u.chip().conversions;
    const int reads = u.chip().reads;

    CHECK_FALSE(u.pass(1000, sample));
    CHECK(u.chip().conversions == conversions + 1);
    CHECK(u.chip().reads == reads);
    CHECK(u.chip().measuring());
    CHECK_FALSE(u.pass(1030, sample));
    CHECK(u.pass(1040, sample));

    const int read_after_edge = u.chip().reads;
    CHECK_FALSE(u.pass(1250, sample));
    CHECK(u.chip().conversions == conversions + 2);
    CHECK(u.chip().reads == read_after_edge);
}

TEST_CASE("board: four conversions a second, one published") {
    Unit u;
    u.sample_instants(0, 1000);
    const int conversions = u.chip().conversions;

    const std::vector<uint32_t> at = u.sample_instants(1000, 11000);

    CHECK(u.chip().conversions - conversions == 40);
    CHECK(at.size() == 10);
    CHECK(u.board.baro_faults() == 0);
}

TEST_CASE("board: triggers stay on the PPS phases") {
    Unit u;

    const std::vector<uint32_t> at = u.trigger_instants(1000, 4000);

    REQUIRE(at.size() == 12);
    for (size_t i = 0; i < at.size(); i++)
        CHECK(at[i] == 1000 + i * runtime::kBaroConversionPeriodMs);
}

TEST_CASE("board: a barometer sample is dated at its conversion, not at its read") {
    Unit u;

    const std::vector<uint32_t> at = u.sample_instants(1000, 1100);

    REQUIRE(at.size() == 1);
    CHECK(at.front() == 1000 + kMidpointMs);
}

TEST_CASE("board: the published sample is the one triggered at the edge") {
    Unit u;
    std::vector<uint32_t> published;
    for (uint32_t t = 0; t < 4000; t += runtime::kServiceStepMs) {
        u.chip().set_pressure_mpa(t % 1000 == 0 ? 95000000 : 90000000);
        events::BaroSample sample{};
        if (u.pass(t, sample)) published.push_back(sample.pressure_mpa);
    }

    REQUIRE(published.size() == 3);
    for (uint32_t mpa : published) CHECK((mpa + 500) / 1000 == 95000);
}

TEST_CASE("board: the barometer is published once a second, on the PPS edge") {
    Unit u;

    const std::vector<uint32_t> at = u.sample_instants(0, 10000);

    REQUIRE(at.size() == 9);
    CHECK(at.front() == 1000 + kMidpointMs);
    for (size_t i = 0; i < at.size(); i++) {
        CHECK((at[i] - kMidpointMs) % 1000 < runtime::kBaroPpsWindowMs);
        if (i > 0) CHECK(at[i] - at[i - 1] == runtime::kBaroPeriodMs);
    }
}

TEST_CASE("board: with no PPS the barometer keeps its own second and its own quarters") {
    Unit u;
    u.platform.pps().set_locked(false);

    const std::vector<uint32_t> at = u.sample_instants(55, 10055);
    const std::vector<uint32_t> triggers = u.trigger_instants(10055, 12055);

    REQUIRE(at.size() >= 9);
    for (size_t i = 1; i < at.size(); i++) CHECK(at[i] - at[i - 1] == runtime::kBaroPeriodMs);
    REQUIRE(triggers.size() == 8);
    for (size_t i = 1; i < triggers.size(); i++)
        CHECK(triggers[i] - triggers[i - 1] == runtime::kBaroConversionPeriodMs);
}

TEST_CASE("board: a conversion that never ends is given up at its ceiling and counted") {
    Unit u;
    u.sample_instants(0, 1000);
    u.chip().extra_measure_us = 10000000;

    CHECK(u.sample_instants(1000, 1000 + runtime::kBaroConversionCeilingMs).empty());
    CHECK(u.board.baro_faults() == 0);
    CHECK(u.sample_instants(1000 + runtime::kBaroConversionCeilingMs, 1500).empty());
    CHECK(u.board.baro_faults() == 1);

    u.chip().extra_measure_us = 0;
    CHECK(u.sample_instants(1500, 2100).size() == 1);
    CHECK(u.board.baro_faults() == 1);
}

TEST_CASE("board: a sample the sensor refused is not a sample of the last pressure") {
    Unit u;
    u.platform.baro().present = false;

    CHECK(u.sample_instants(1000, 5000).empty());
    CHECK(u.board.baro_faults() == 16);
}
