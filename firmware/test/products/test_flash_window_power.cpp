// The settings write when the cell is failing: held while it is critical, never
// touched at the cutoff, flushed once the cell recovers, and stopped by a fired
// power-failure comparator. Split from test_flash_window.cpp, which keeps the
// dwell the write has to dodge.
#include "core/events/link.h"
#include "core/timing/durable_write.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/input/gesture.h"
#include "products/skyblip_go/pages/menu.h"
#include "test/support/product_rig.h"
#include "test/support/settings_writes.h"

using namespace skyblip;
using namespace skyblip::settings_writes;

// E1 at product scale. The cell is below the warning, so the settings sector is
// not touched at all - and the change is not thrown away either: it stays dirty,
// which is what makes a charger arriving still save it.
TEST_CASE("flash window: a change is held, not written, while the cell is critical") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.platform.battery().millivolts = 3400;
    // The board samples the cell once a second and the monitor acts on the third
    // consecutive reading, so a low cell takes four seconds to become a decision.
    rig.seconds(t, 5, /*speed_mm_s=*/0, 0);
    REQUIRE(rig.state().power.level == power::PowerLevel::Critical);

    const uint32_t before = writes(rig);
    change_volume(rig, 5);
    step_until(rig, t, t + timing::DurableWriteWindow::kMaxDeferMs + 2000);
    CHECK(writes(rig) == before);
    // Refused, counted once, and never handed to the placement policy - a pending
    // change is one the bound would eventually force onto flash, which is the
    // write this refuses.
    CHECK(rig.product.config().refused_writes() == 1);
    CHECK(rig.product.config().holding_for_power());
    CHECK(rig.product.config().durable_writes().requests() == 0);
    CHECK(rig.product.config().durable_writes().forced() == 0);

    // The cable arrives. The terminal is held above the cell, core/power reports
    // Normal, and the change a pilot made is still there to write.
    rig.platform.battery().external_power = true;
    rig.seconds(t, 3, /*speed_mm_s=*/0, 0);
    REQUIRE(rig.state().power.level == power::PowerLevel::Normal);
    CHECK_FALSE(rig.product.config().holding_for_power());
    CHECK(writes(rig) - before == 1);
    CHECK(rig.product.config().durable_writes().forced() == 0);

    uint8_t blob[64];
    size_t n = 0;
    REQUIRE(rig.platform.kv().read("settings", blob, sizeof(blob), n) == Status::Ok);
    go::Settings stored{};
    REQUIRE(go::from_blob(blob, n, stored) == Status::Ok);
    CHECK(stored.alarm_volume == 5);
}

// The one power-off that does not flush, and it is the whole trade: a cell at its
// cutoff takes the flight log record with it and leaves the settings sector
// alone. What is lost is the change a pilot was making; what is protected is
// every change they ever made.
TEST_CASE("flash window: a cell at its cutoff powers off without touching the settings") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.platform.battery().millivolts = 3400;
    rig.seconds(t, 5, /*speed_mm_s=*/0, 0);
    REQUIRE(rig.state().power.level == power::PowerLevel::Critical);
    const uint32_t before = writes(rig);

    // A change made on a cell that is already warning: held, never written.
    change_volume(rig, 5);
    rig.seconds(t, 2, /*speed_mm_s=*/0, 0);
    REQUIRE(writes(rig) == before);

    // And now the cell reaches its cutoff, which is the flush this refuses.
    rig.platform.battery().millivolts = 3100;
    rig.seconds(t, 5, /*speed_mm_s=*/0, 0);
    REQUIRE(rig.product.shutdown().reason() == power::ShutdownReason::LowBattery);
    REQUIRE(rig.product.shutdown().phase() != power::ShutdownPhase::Running);
    CHECK(writes(rig) == before);
    CHECK(rig.product.config().refused_writes() == 1);
    CHECK(rig.product.config().holding_for_power());
}

// A deliberate power-off on a healthy cell is the other half of the same rule,
// and it does flush: a pilot who changed a setting and switched the device off
// must not find the old value.
TEST_CASE("flash window: a healthy cell flushes the change it was holding") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t);
    step_until(rig, t, t + static_cast<uint32_t>(timing::kDirectStart) + 20);
    REQUIRE(rig.state().rf.plan.tx_allowed);
    const uint32_t before = writes(rig);

    change_volume(rig, 5);
    step_one(rig, t);
    REQUIRE(writes(rig) == before);

    rig.product.shutdown().request(power::ShutdownReason::LinkRequest, t);
    step_one(rig, t);
    CHECK(writes(rig) - before == 1);
    CHECK(rig.product.config().refused_writes() == 0);
}

// POFCON, through the whole product: the comparator fires in interrupt context on
// the silicon, the product polls the latch once a pass, and the settings writer
// stops. Nothing on this path re-reads the divider.
TEST_CASE("flash window: a fired power-failure comparator stops settings writes") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    stand_on_the_ground(rig, t);
    REQUIRE(rig.state().power.level == power::PowerLevel::Normal);
    REQUIRE(rig.platform.system_power().supply_monitor_armed());

    const uint32_t before = writes(rig);
    rig.platform.system_power().supply_warning = true;
    step_one(rig, t);
    CHECK(rig.product.power().supply_warned());
    CHECK(rig.product.power().supply_warnings() == 1);
    // Read and cleared, so one warning is one warning however many passes run.
    step_until(rig, t, t + 200);
    CHECK(rig.product.power().supply_warnings() == 1);

    change_volume(rig, 5);
    step_until(rig, t, t + timing::DurableWriteWindow::kMaxDeferMs + 2000);
    CHECK(writes(rig) == before);
    // Latching: a rail that came back up does not make it un-happen.
    CHECK_FALSE(rig.product.power().may_write(power::DurableWrite::Settings));
    CHECK(rig.product.power().may_write(power::DurableWrite::FlightRecord));
}
