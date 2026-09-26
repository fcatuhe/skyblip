// The seven cumulative duty counters on bus::State, through the service that owns what each counts.
#include "core/bus/state.h"
#include "core/gnss/first_fix.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/services/radio.h"
#include "simulator/simulator.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

constexpr uint32_t kPassMs = 10;

// The uplink window plus the two M-band dwells either side of the hop guard.
constexpr uint32_t kDwellMsPerSecond =
    static_cast<uint32_t>((timing::kUplinkRxEnd - timing::kUplinkRxStart) +
                          (timing::kSlot0End - timing::kHopGuardMs - timing::kSlot0Start) +
                          (timing::kSlot1End - timing::kSlot1Start));

// The radio service alone over ports::NullRf, which answers Down to every plan.
struct RefusingExecutor {
    platform::host::Clock clock;
    ports::NullRoles null;
    ports::Roles roles{
        clock,          null.rf,          null.link, null.display,         null.kv,
        null.log_flash, null.annunciator, null.dfu,  null.die_temperature, null.indicator,
        null.gnss};
    bus::Bus bus{};
    bus::State state{};
    diag::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::Settings settings{};
    go::RadioService radio{context, settings};

    RefusingExecutor() { roles.capabilities = ports::Capability::Rf; }

    void run(uint32_t until_ms) {
        for (uint32_t t = 0; t <= until_ms; t += kPassMs) {
            clock.set_millis(t);
            radio.tick(t);
        }
    }
};

}  // namespace

TEST_CASE("duty: a presented frame counts a partial refresh and a still glass counts none") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.run(t, t + 3000, kPassMs);
    t += 3000;
    const uint32_t presented = rig.state().duty.panel_partial_refreshes;
    CHECK(presented > 0);

    rig.run(t, t + 5000, kPassMs);
    t += 5000;
    CHECK(rig.state().duty.panel_partial_refreshes == presented);

    rig.tap(t);
    rig.run(t, t + 2000, kPassMs);
    CHECK(rig.state().duty.panel_partial_refreshes > presented);
}

// A page swap wipes the glass black on the partial waveform: pricing it as a full is five partials.
TEST_CASE("duty: the parked frame is the only full refresh a page swap or a present pays for") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.run(t, t + 2000, kPassMs);
    t += 2000;
    rig.tap(t);
    rig.run(t, t + 3000, kPassMs);
    t += 3000;
    CHECK(rig.state().duty.panel_full_refreshes == 0);

    const uint32_t partials = rig.state().duty.panel_partial_refreshes;
    rig.product.screen().park_for_off();
    rig.run(t, t + 3000, kPassMs);
    CHECK(rig.state().duty.panel_full_refreshes == 1);
    CHECK(rig.state().duty.panel_partial_refreshes == partials);
}

TEST_CASE("duty: the backlight counter runs while the lamp is lit and stops with it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.run(t, t + 2000, kPassMs);
    t += 2000;
    CHECK(rig.state().duty.backlight_ms == 0);

    rig.product.screen().set_backlight(true);
    rig.run(t, t + 3000, kPassMs);
    t += 3000;
    const uint32_t lit_ms = rig.state().duty.backlight_ms;
    CHECK(lit_ms >= 3000 - kPassMs);
    CHECK(lit_ms <= 3000 + kPassMs);

    rig.product.screen().set_backlight(false);
    rig.run(t, t + 2000, kPassMs);
    CHECK(rig.state().duty.backlight_ms <= lit_ms + kPassMs);
}

TEST_CASE("duty: the receiver counter advances by what the executor was armed for") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 10000, kPassMs);
    const uint32_t armed_ms = rig.state().duty.rx_armed_ms;
    // ten seconds of dwells, less at most one pass at each of the three edges
    CHECK(armed_ms <= 10 * kDwellMsPerSecond);
    CHECK(armed_ms >= 10 * (kDwellMsPerSecond - 3 * kPassMs));
}

// The counter follows the radio, not the map: a refused plan was never armed.
TEST_CASE("duty: a dwell the executor refuses is not counted, whatever the slot map says") {
    RefusingExecutor rig;
    REQUIRE(rig.radio.setup() == Status::Ok);
    rig.run(5000);
    CHECK(rig.state.rf.plan.end_ms > rig.state.rf.plan.start_ms);
    CHECK(rig.state.duty.rx_armed_ms == 0);
}

// The disagreement a pass that ran long makes on silicon: the map says 989 ms a second regardless.
TEST_CASE("duty: a pass too coarse for the dwell edges reads below the slot map") {
    Rig fine;
    REQUIRE(fine.setup() == Status::Ok);
    fine.run(0, 10000, kPassMs);

    Rig coarse;
    REQUIRE(coarse.setup() == Status::Ok);
    coarse.run(0, 10000, 250);

    CHECK(coarse.state().duty.rx_armed_ms > 0);
    // a whole second of receive lost over ten, with the same map in front of both
    CHECK(coarse.state().duty.rx_armed_ms + 1000 < fine.state().duty.rx_armed_ms);
}

TEST_CASE("duty: the transmit counter is the air time the hour's own budget is spent from") {
    simulator::Simulator sim;
    REQUIRE(sim.setup() == Status::Ok);
    sim.world().set_fix(true);
    sim.world().set_speed_kt(50);
    sim.run(gnss::kFirstFixSettleMs + 8000);

    const bus::State& state = sim.product().state();
    REQUIRE(state.air.tx_ok > 0);
    CHECK(state.duty.tx_keyed_ms >= timing::Transmitter::kAirTimeMs);
    CHECK(state.duty.tx_keyed_ms == sim.product().radio().transmitter().air_time().total_ms());
}

// F5 holds every burst until the first solutions settle: nothing on air is nothing counted.
TEST_CASE("duty: a device that never keyed the transmitter counts no air time") {
    simulator::Simulator sim;
    REQUIRE(sim.setup() == Status::Ok);
    sim.world().set_fix(true);
    sim.world().set_pps_locked(false);
    sim.run(gnss::kFirstFixSettleMs + 8000);

    REQUIRE(sim.product().state().air.tx_ok == 0);
    CHECK(sim.product().state().duty.tx_keyed_ms == 0);
}

TEST_CASE("duty: the annunciator counter runs while it sounds and stands still while muted") {
    simulator::Simulator sim;
    REQUIRE(sim.setup() == Status::Ok);
    const uint32_t step = simulator::Simulator::kStepMs;
    for (uint32_t t = 0; t <= 3000; t += step) sim.step(t);
    const uint32_t before = sim.product().state().duty.annunciator_ms;

    sim.world().add_threat();
    for (uint32_t t = 3000; t <= 9000; t += step) sim.step(t);
    REQUIRE(sim.announcing_level() != traffic::Level::None);
    const uint32_t sounded = sim.product().state().duty.annunciator_ms;
    CHECK(sounded > before);

    sim.product().settings().alarm_enabled = false;
    for (uint32_t t = 9000; t <= 15000; t += step) sim.step(t);
    CHECK(sim.product().state().duty.annunciator_ms == sounded);
}

TEST_CASE("duty: the BLE counter runs while a central is connected and stops when it goes") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.run(t, t + 2000, kPassMs);
    t += 2000;
    CHECK(rig.state().duty.ble_connected_ms == 0);

    rig.raise_link();
    rig.run(t, t + 3000, kPassMs);
    t += 3000;
    REQUIRE(rig.link_up());
    const uint32_t connected_ms = rig.state().duty.ble_connected_ms;
    CHECK(connected_ms >= 3000 - 2 * kPassMs);
    CHECK(connected_ms <= 3000 + 2 * kPassMs);

    rig.drop_link();
    rig.run(t, t + 3000, kPassMs);
    CHECK(rig.state().duty.ble_connected_ms <= connected_ms + 2 * kPassMs);
}
