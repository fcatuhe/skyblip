// ETSI EN 300 220-2 V3.3.1, band M's row of Table 4 and clause 4.6, read against this device.
#include <cstdint>

#include "core/timing/channel.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "hardware/platform/host/rf.h"
#include "protocols/adsl/physical.h"
#include "simulator/simulator.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

// Table 4, band M: 868.000 MHz to 868.600 MHz, 25 mW e.r.p., which is 13.98 dBm.
TEST_CASE("EN 300 220-2 4.2.4: own-ship transmits inside band M, at no more than 25 mW e.r.p.") {
    for (const uint32_t centre_hz : {adsl::kMBand0Hz, adsl::kMBand1Hz}) {
        CHECK(centre_hz - adsl::kMBandChannelBandwidthHz / 2 >= 868000000u);
        CHECK(centre_hz + adsl::kMBandChannelBandwidthHz / 2 <= 868600000u);
    }
    CHECK(parts::sx1262::kResultingErpCentiDb <= 1398);
}

// 1 % of Tobs, an hour (4.4.3.2): the route this device declares, kept beside polite access.
TEST_CASE("EN 300 220-2 4.4.3.3: the air time of any hour stays within band M's 1 % duty cycle") {
    timing::AirTime air;
    CHECK(timing::AirTime::kWindowMs == 3600000u);
    CHECK(timing::AirTime::kBudgetMs == 36000u);
    air.spend(0, timing::AirTime::kBudgetMs);
    CHECK_FALSE(air.may_spend(0, 1));
    CHECK(air.may_spend(timing::AirTime::kWindowMs + 1, 1));

    // And the transmitter asks it: a burst past the hour's budget is refused, not sent.
    timing::ClockState clock{};
    clock.utc_valid = true;
    clock.pps_locked = true;
    const timing::SlotPlan plan = timing::Scheduler::plan(500, clock);
    timing::Transmitter transmitter;
    transmitter.configure(0x5B7E57);
    CHECK(transmitter.attempt(plan, 7200, 0, true, 0).go);
    for (uint32_t burst = 0; burst < timing::AirTime::kBudgetMs / timing::Transmitter::kAirTimeMs;
         burst++)
        transmitter.sent(burst, 0);
    const timing::Transmitter::Attempt refused = transmitter.attempt(plan, 7200, 0, true, 0);
    CHECK_FALSE(refused.go);
    CHECK(refused.over_budget);
}

TEST_CASE("EN 300 220-2 4.6.1.0: a held channel defers a burst, which goes once it clears") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    uint32_t now_ms = past_settling(simulator);
    const int8_t held_dbm[1] = {-50};
    models::Sx1262& chip = simulator.platform().chips().radio;
    chip.set_rssi_sequence(held_dbm, 1);
    const uint32_t until_ms = now_ms + 3000;
    while (simulator.product().state().air.tx_deferred == 0 && now_ms < until_ms)
        simulator.step(++now_ms);
    chip.set_rssi_sequence(held_dbm, 0);
    REQUIRE(simulator.product().state().air.tx_deferred == 1);
    const uint32_t ok_before = simulator.product().state().air.tx_ok;
    run_on(simulator, now_ms + 1, 1000, 1);
    CHECK(simulator.product().state().air.tx_ok > ok_before);
    CHECK(simulator.product().state().air.tx_busy_missed == 0);
}

// INFO: fc 14nov26 optional: AFA is one algorithm 4.6.1.0 lists, ADS-L's second names the channel
TEST_CASE("EN 300 220-2 4.6.1.1: a channel found busy is left for another operating frequency" *
          doctest::skip()) {
    FAIL(
        "optional: no AFA, a deferred burst keeps the channel C.2.5's alternation gave its second");
}

// Table 17 repeats -1 Table 45 over Table 11, which repeats -1 Table 32.
TEST_CASE(
    "EN 300 220-2 4.6.2.3: the CCA threshold at the connector is Table 17's, for this antenna") {
    const int32_t at_connector = platform::kCcaThresholdCentiDbm + parts::sx1262::kFeedLossCentiDb;
    const int32_t antenna_dbd = 160 - 215;
    CHECK(at_connector == -9399 + 1500 - antenna_dbd);
    const int32_t erp_under_100_mw_centi_dbm = 2000;
    CHECK(parts::sx1262::kResultingErpCentiDb < erp_under_100_mw_centi_dbm);
}

TEST_CASE("EN 300 220-2 4.6.3.2: the polite access timing stays inside Table 18") {
    CHECK(timing::PoliteAccess::kCcaWindowUs >= 160);
    CHECK(timing::PoliteAccess::kTransmissionCeilingUs <= 1000000);
    CHECK(timing::AirTime::kBudgetMs <= 100000);
    CHECK(timing::PoliteAccess::kSameFrequencyOffFloorMs >= 100);
    CHECK(timing::Transmitter::kAirTimeMs * 1000 <= timing::PoliteAccess::kTransmissionCeilingUs);
}
