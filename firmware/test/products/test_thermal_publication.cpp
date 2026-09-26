// What the power service publishes about temperature, and how long a reading stands.
#include "core/events/sensor.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/clock.h"
#include "hardware/platform/host/die_temperature.h"
#include "ports/null.h"
#include "products/skyblip_go/services/power.h"
#include "products/skyblip_go/settings.h"

using namespace skyblip;

namespace {

struct Rig {
    platform::host::Clock clock;
    ports::NullRoles null;
    platform::host::DieTemperature sensor{};
    ports::Roles roles{clock,   null.rf,        null.link,        null.display,
                       null.kv, null.log_flash, null.annunciator, null.dfu,
                       sensor,  null.indicator, null.gnss};
    bus::Bus bus{};
    bus::State state{};
    diag::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::Settings settings{};
    go::PowerService power{context, settings};

    Rig() { roles.capabilities = ports::Capability::DieTemperature; }

    void tick_on_cable(uint32_t now_ms) {
        bus.battery.push(events::BatterySample{4100, /*external_power=*/true});
        power.tick(now_ms);
    }
};

constexpr uint32_t kPeriod = go::PowerService::kDieStaleMs / 3;

}  // namespace

TEST_CASE("thermal: the freshness window is spelled out in seconds, not left to drift") {
    CHECK(go::PowerService::kDieStaleMs == 30000);
}

TEST_CASE("thermal: a reading the sensor gave is published where the panel gate reads it") {
    Rig rig;
    rig.sensor.hold(421);
    rig.power.tick(1000);

    CHECK(rig.state.power.die_valid);
    CHECK(rig.state.power.die_dc == 421);
}

TEST_CASE("thermal: a board with no sensor publishes no reading, never a zero") {
    Rig rig;
    rig.roles.capabilities = ports::Capabilities{};
    rig.power.tick(1000);

    CHECK_FALSE(rig.state.power.die_valid);
    CHECK(rig.sensor.reads() == 0);
}

// The gate would otherwise hold a hot panel for ever on a reading nobody refreshed.
TEST_CASE("thermal: a sensor that stops answering stops gating the panel") {
    Rig rig;
    rig.sensor.hold(700);
    uint32_t t = 1000;
    rig.power.tick(t);
    REQUIRE(rig.state.power.die_valid);

    rig.sensor.refuse();
    for (uint32_t elapsed = kPeriod; elapsed <= go::PowerService::kDieStaleMs; elapsed += kPeriod) {
        rig.power.tick(t + elapsed);
        CHECK(rig.state.power.die_valid);
    }

    rig.power.tick(t + go::PowerService::kDieStaleMs + kPeriod);
    CHECK_FALSE(rig.state.power.die_valid);
    CHECK(rig.state.power.die_dc == 700);
}

TEST_CASE("thermal: one reading and then silence, and the charge window stops saying Ok") {
    Rig rig;
    rig.sensor.hold(250);
    const uint32_t t = 1000;
    rig.tick_on_cable(t);
    REQUIRE(rig.state.power.charge == power::ChargeCondition::Ok);

    rig.sensor.refuse();
    rig.tick_on_cable(t + kPeriod);
    CHECK(rig.state.power.charge == power::ChargeCondition::Ok);

    rig.tick_on_cable(t + go::PowerService::kDieStaleMs + kPeriod);
    CHECK(rig.state.power.charge != power::ChargeCondition::Ok);
}

// Go's charger has no enable pin, so the window can only warn: stale means say nothing, not stop.
TEST_CASE("thermal: a stale reading on Go's cable cannot say, and never counts as a warning") {
    Rig rig;
    rig.sensor.hold(20);
    const uint32_t t = 1000;
    rig.tick_on_cable(t);
    REQUIRE(rig.state.power.charge == power::ChargeCondition::TooCold);
    REQUIRE(rig.power.charge_warnings() == 1);

    rig.sensor.refuse();
    rig.tick_on_cable(t + go::PowerService::kDieStaleMs + kPeriod);
    CHECK(rig.state.power.charge == power::ChargeCondition::Unknown);
    CHECK(rig.power.charge_warnings() == 1);
}

TEST_CASE("thermal: a sensor that answers again is believed again") {
    Rig rig;
    uint32_t t = 1000;
    rig.power.tick(t);
    rig.sensor.refuse();
    rig.power.tick(t + go::PowerService::kDieStaleMs + kPeriod);
    REQUIRE_FALSE(rig.state.power.die_valid);
    rig.sensor.hold(180);
    rig.power.tick(t + go::PowerService::kDieStaleMs + 2 * kPeriod);
    CHECK(rig.state.power.die_valid);
    CHECK(rig.state.power.die_dc == 180);
}

TEST_CASE("thermal: the freshness window spans the 49.7-day wrap of the counter") {
    Rig rig;
    const uint32_t before_wrap = 0xFFFFFFFFu - kPeriod;
    rig.power.tick(before_wrap);
    REQUIRE(rig.state.power.die_valid);

    rig.sensor.refuse();
    rig.power.tick(before_wrap + kPeriod);
    CHECK(rig.state.power.die_valid);

    rig.power.tick(before_wrap + go::PowerService::kDieStaleMs + 2 * kPeriod);
    CHECK_FALSE(rig.state.power.die_valid);
}

TEST_CASE("thermal: the supply warning the panel gate reads is the monitor's own") {
    Rig rig;
    rig.power.tick(1000);
    REQUIRE_FALSE(rig.state.power.supply_warned);

    rig.power.on_supply_warning();
    rig.power.tick(2000);
    CHECK(rig.state.power.supply_warned);
}
