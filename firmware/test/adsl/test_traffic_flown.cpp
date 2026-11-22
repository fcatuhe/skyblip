// ADS-L 4 SRD-860 issue 2 G.1.1 and G.1.16 flown: every Traffic burst against the solution it
// names.
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "simulator/simulator.h"
#include "test/support/rf_pass_rig.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

// Turning and climbing, so a carried position or height is not the one the receiver solved.
void fly_measured(simulator::Simulator& simulator,
                  uint32_t latency_ms = parts::L76k::kBurstStartMs) {
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(120);
    simulator.world().set_turn_dps(6);
    simulator.world().set_climb_mm_s(20000);
    simulator.world().gnss().latency_ms = latency_ms;
}

// The two clauses read literally, for one burst: which solution, named how, known when, how old.
int check_flown(simulator::Simulator& simulator, const simulator::AirRecord& record) {
    const models::L76k& chip = simulator.world().gnss();
    adsl::Packet packet{};
    REQUIRE(packet_of(record, packet));
    const models::L76k::Solved* solved = solution_named(chip, packet, record.at_us);
    REQUIRE(solved != nullptr);
    const uint32_t on_air_ms = static_cast<uint32_t>(record.at_us / 1000);
    const int named_ms = static_cast<int>(packet.timestamp % 4u * adsl::kTimestampQuarterMs);
    CHECK(std::abs(static_cast<int>(solved->at_ms % 1000) - named_ms) <= 10);
    CHECK(std::abs(static_cast<int>(solved->utc_ms) - named_ms) <= 10);
    CHECK(solved->at_ms + chip.latency_ms <= on_air_ms);
    CHECK(on_air_ms + adsl::kAirTimeMs - solved->at_ms <= 500u);

    adsl::Packet measured{};
    measured.reset();
    measured.set_latitude_1e7(solved->latitude_1e7);
    measured.set_longitude_1e7(solved->longitude_1e7);
    measured.set_altitude_m((solved->altitude_hae_mm + 500) / 1000);
    CHECK(std::abs(packet.latitude_steps() - measured.latitude_steps()) <= 1);
    CHECK(std::abs(packet.longitude_steps() - measured.longitude_steps()) <= 1);
    CHECK(std::abs(packet.altitude_m() - measured.altitude_m()) <= 1);
    return 1;
}

int check_every_position(simulator::Simulator& simulator) {
    int checked = 0;
    for (const simulator::AirRecord& record :
         records_of(simulator.world().air(), adsl::Packet::kPayloadTypePosition))
        checked += check_flown(simulator, record);
    return checked;
}

}  // namespace

// Each burst carries one solution unextrapolated, known before it left, named within the 10 ms.
TEST_CASE("ADS-L.4.SRD860.G.1.1: every Traffic burst names its solution's time within 10 ms") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    fly_measured(simulator);
    run_on(simulator, past_settling(simulator), 30000);
    CHECK(check_every_position(simulator) >= 25);
}

// The pinned case was a drain mixing two solutions: a .500 pair stalled 120 ms flies whole or not.
TEST_CASE("ADS-L.4.SRD860.G.1.1: a height and a position from two solutions never go on air") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    fly_measured(simulator);
    models::L76k& chip = simulator.world().gnss();
    chip.stall_period_ms = 2000;
    chip.stall_from_ms = 1520;
    chip.stall_until_ms = 1640;
    run_on(simulator, past_settling(simulator), 40000);

    int behind_the_stall = 0;
    for (const simulator::AirRecord& record :
         records_of(simulator.world().air(), adsl::Packet::kPayloadTypePosition)) {
        check_flown(simulator, record);
        const uint32_t phase_ms = static_cast<uint32_t>(record.at_us / 1000 % 2000);
        if (phase_ms >= chip.stall_until_ms && phase_ms < 2000) behind_the_stall++;
    }
    CHECK(behind_the_stall > 0);
}

// The pinned case: a solution a second late was dated at the edge it landed after, and flown.
TEST_CASE("ADS-L.4.SRD860.G.1.16: a solution that lands a second late never goes on air") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    fly_measured(simulator);
    const uint32_t now_ms = past_settling(simulator);
    const uint32_t held_before = simulator.product().state().air.tx_held_for_fix;
    simulator.world().gnss().latency_ms = 1000 + parts::L76k::kBurstStartMs;
    run_on(simulator, now_ms, 20000);

    check_every_position(simulator);
    CHECK(simulator.product().state().air.tx_held_for_fix > held_before + 10);
}

// The host's side of a cold start: three seconds of sentences waiting in the port, drained at once.
TEST_CASE("ADS-L.4.SRD860.G.1.16: a backlog of old sentences at a cold start never goes on air") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    fly_measured(simulator);
    models::L76k& chip = simulator.world().gnss();
    chip.stall_period_ms = 1000000000;
    chip.stall_from_ms = 0;
    chip.stall_until_ms = 3000;
    run_on(simulator, past_settling(simulator), 20000);

    CHECK(simulator.product().state().clock.utc_anchor_fixed);
    CHECK(check_every_position(simulator) >= 15);
}

// TODO: fc 18nov26 a receiver a second late from power-up is unmeasured, until the PPS bench item
TEST_CASE("ADS-L.4.SRD860.G.1.16: an L76K that delivers every solution a second late is refused" *
          doctest::skip()) {
    FAIL("unmeasured: no bench has shown the L76K delivers each solution within 250 ms of it");
}

// The pinned case: a part at 5 Hz passed a quarter count and a .600 solution went out named .500.
TEST_CASE("ADS-L.4.SRD860.G.1.1: a receiver solving off the quarters never names a fix wrongly") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    fly_measured(simulator);
    simulator.world().gnss().fast_rate_runs_at_ms = 200;
    run_on(simulator, past_settling(simulator), 30000);

    CHECK_FALSE(simulator.product().board().gnss().solving_half_seconds());
    check_every_position(simulator);
}

TEST_CASE("ADS-L.4.SRD860.G.1.16: a key delay cannot age the encoded fix past its deadline") {
    Pass pass;
    REQUIRE(pass.begin() == Status::Ok);
    pass.state.activation.serial = 1199;
    pass.state.own.utc_s = 2;
    pass.state.own.fix_at_ms = 2000;
    pass.state.own.fix_in_hand_ms = 2070;
    pass.poll_clock(2420000);
    pass.rf.service(2420);
    pass.radio_service.tick(2420);
    pass.whole_pass(2450000);
    pass.whole_pass(2489000);
    REQUIRE(pass.state.rf.dwell.burst_armed);

    pass.whole_pass(2491000);

    CHECK_FALSE(pass.chip.saw_command(parts::sx1262::kSetTx));
    CHECK(pass.state.air.tx_lost == 1);
}

// Flown with the channel busy one burst in three seconds, and a receiver 200 ms late on the wire.
TEST_CASE("ADS-L.4.SRD860.G.1.16: no Traffic burst goes on air over 500 ms after its solution" *
          doctest::test_suite("main")) {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    fly_measured(simulator, 200);
    uint32_t now_ms = past_settling(simulator);
    const bus::State& state = simulator.product().state();
    models::Sx1262& radio = simulator.platform().chips().radio;
    const int8_t busy_dbm[1] = {-50};
    std::vector<uint64_t> busy_at_us;
    for (uint32_t second = 0; second < 40; second++) {
        const uint32_t reports = state.rf.busy_reports;
        bool held = second % 3 == 0;
        radio.set_rssi_sequence(busy_dbm, held ? 1 : 0);
        for (uint32_t step = 0; step < 1000; step += simulator::Simulator::kStepMs) {
            simulator.step(now_ms += simulator::Simulator::kStepMs);
            if (!held || state.rf.busy_reports == reports) continue;
            radio.set_rssi_sequence(busy_dbm, 0);
            held = false;
            busy_at_us.push_back(state.rf.busy_at(reports)->tx_at_us);
        }
    }
    radio.set_rssi_sequence(busy_dbm, 0);
    CHECK(state.air.tx_deferred > 0);
    CHECK(check_every_position(simulator) >= 20);

    int deferred_flown = 0;
    for (const uint64_t busy_us : busy_at_us) {
        const bool on_a = busy_us / 1000 % 1000 < static_cast<uint64_t>(timing::kWindowAMs.end_ms);
        for (const simulator::AirRecord& record :
             records_of(simulator.world().air(), adsl::Packet::kPayloadTypePosition))
            if (record.at_us > busy_us &&
                record.at_us - busy_us <= adsl::kMBandBackoff.ceiling_us &&
                simulator::Air::tuned_to(record.frequency_hz,
                                         on_a ? adsl::kMBand0Hz : adsl::kMBand1Hz))
                deferred_flown++;
    }
    MESSAGE(busy_at_us.size() << " bursts found the channel busy, " << deferred_flown
                              << " positions flew after their deferral");
    CHECK(deferred_flown > 0);
}

// A receiver left at 1 Hz holds most airborne bursts, its solution too old by the time they fly.
TEST_CASE(
    "ADS-L.4.SRD860.G.1.16: two solutions a second and the redraw put a burst in every second") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    fly_measured(simulator);
    const uint32_t now_ms = past_settling(simulator);
    const uint32_t solutions = simulator.product().state().flight.gnss_solutions;
    run_on(simulator, now_ms, 20000 - simulator::Simulator::kStepMs);

    CHECK(simulator.product().board().gnss().solving_half_seconds());
    CHECK(simulator.product().state().flight.gnss_solutions - solutions == 40);
    std::vector<uint32_t> seconds;
    for (const simulator::AirRecord& record :
         records_of(simulator.world().air(), adsl::Packet::kPayloadTypePosition))
        seconds.push_back(static_cast<uint32_t>(record.at_us / 1000000));
    CHECK(seconds.size() == 20);
    for (size_t i = 1; i < seconds.size(); i++) CHECK(seconds[i] == seconds[i - 1] + 1);
}
