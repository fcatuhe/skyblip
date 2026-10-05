// What the board publishes off the divider: every pass, read or refused, with the cable beside it.
#include <vector>

#include "boards/lilygo/t_echo_plus/board.h"
#include "core/bus/bus.h"
#include "core/bus/state.h"
#include "core/events/sensor.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/platform.h"
#include "runtime/tasks.h"

using namespace skyblip;

namespace {

using Board = boards::TEchoPlus<platform::host::Platform>;

struct Unit {
    Unit() { REQUIRE(board.begin() == Status::Ok); }

    std::vector<events::BatterySample> passes(int count) {
        std::vector<events::BatterySample> published;
        for (int i = 0; i < count; i++) {
            t += runtime::kBatteryPeriodMs;
            platform.clock().set_millis(t);
            board.poll(state, t);
            events::BatterySample sample{};
            while (bus.battery.pop(sample)) published.push_back(sample);
        }
        return published;
    }

    platform::host::Platform platform;
    bus::Bus bus;
    Board board{platform, bus};
    bus::State state;
    uint32_t t{0};
};

}  // namespace

// 0B1B2C's divider sat over the window for a whole charge, and hid the cable with it (#114).
TEST_CASE("board battery: a refused reading still publishes the cable and what was read") {
    Unit unit;
    unit.platform.battery().external_power = true;
    unit.platform.battery().millivolts = 4812;

    const std::vector<events::BatterySample> published = unit.passes(3);
    REQUIRE(published.size() == 3);
    for (const events::BatterySample& sample : published) {
        CHECK(sample.external_power);
        CHECK(sample.millivolts == 4812);
    }
}

TEST_CASE("board battery: an ADC that gave nothing still publishes the cable, at zero") {
    Unit unit;
    unit.platform.battery().external_power = true;
    unit.platform.battery().present = false;

    const std::vector<events::BatterySample> published = unit.passes(3);
    REQUIRE(published.size() == 3);
    for (const events::BatterySample& sample : published) {
        CHECK(sample.external_power);
        CHECK(sample.millivolts == 0);
    }
}

TEST_CASE("board battery: a believable reading off the cable goes out as it was read") {
    Unit unit;
    unit.platform.battery().millivolts = 3870;

    const std::vector<events::BatterySample> published = unit.passes(2);
    REQUIRE(published.size() == 2);
    CHECK(published.back().millivolts == 3870);
    CHECK_FALSE(published.back().external_power);
}
