// The transmit gate on a flying Go: each requirement, on the radio and receiver behind it.
#include <cstdint>

#include "core/timing/slot.h"
#include "core/timing/transmit_gate.h"
#include "doctest/doctest.h"
#include "simulator/simulator.h"
#include "test/support/armings_rig.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

const timing::TransmitGate& gate_of(simulator::Simulator& simulator) {
    return simulator.product().state().transmit;
}

// INFO: fc 19nov26 a pass every 100 ms through the seconds from first_s, as the loop runs
void tick_seconds(Armings& armings, uint32_t first_s, uint32_t seconds) {
    for (uint32_t second = first_s; second < first_s + seconds; second++)
        for (int phase_ms = 0; phase_ms < 1000; phase_ms += 100) armings.tick_in(second, phase_ms);
}

}  // namespace

TEST_CASE(
    "rf gate: a Go with a settled 2 Hz fix, an edge and a radio transmits, and names nothing") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    run_on(simulator, past_settling(simulator), 5000);
    CHECK(gate_of(simulator).transmits());
    CHECK(gate_of(simulator).shown() == timing::NoTx::None);
    CHECK(count_payloads(simulator.world().air(), adsl::Packet::kPayloadTypeStatus) +
              count_payloads(simulator.world().air(), adsl::Packet::kPayloadTypePosition) >
          0);
}

// D.3 listens on the channel readings, so a radio that stops taking them cannot listen.
TEST_CASE(
    "rf gate: a radio that stops answering stops the bursts at once and reads RADIO 10 s on") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    const uint32_t settled_ms = past_settling(simulator);
    run_on(simulator, settled_ms, 2000);
    REQUIRE(gate_of(simulator).transmits());

    simulator.platform().chips().radio.busy_stuck = true;
    const uint32_t stuck_ms = settled_ms + 2005;
    run_on(simulator, stuck_ms, go::RadioService::kRadioAnswerMaxAgeMs + 500);
    CHECK_FALSE(gate_of(simulator).transmits());
    CHECK(gate_of(simulator).unmet() == timing::NoTx::Radio);
    CHECK(gate_of(simulator).shown() == timing::NoTx::None);
    run_on(simulator, stuck_ms + go::RadioService::kRadioAnswerMaxAgeMs + 505,
           timing::TransmitGate::kNoTxHoldMs);
    CHECK(gate_of(simulator).shown() == timing::NoTx::Radio);
}

// G.1.1 and G.1.16 hold only for a receiver solving twice a second on .000 and .500.
TEST_CASE("rf gate: a receiver that will not take 2 Hz never transmits, and reads GNSS") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().gnss().refuses_fast_rate = true;
    simulator.world().set_fix(true);
    run_on(simulator, past_settling(simulator), 15000);
    CHECK(simulator.product().state().own.fix_valid);
    CHECK_FALSE(gate_of(simulator).transmits());
    CHECK(gate_of(simulator).shown() == timing::NoTx::Gnss);
    CHECK(count_payloads(simulator.world().air(), adsl::Packet::kPayloadTypePosition) == 0);
}

TEST_CASE("rf gate: a PPS lost past its holdover stops the bursts and reads PPS") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    const uint32_t settled_ms = past_settling(simulator);
    simulator.world().set_pps_locked(false);
    run_on(simulator, settled_ms, timing::kPpsHoldoverMs - 1000);
    CHECK(gate_of(simulator).transmits());
    run_on(simulator, settled_ms + timing::kPpsHoldoverMs - 995, 2000);
    CHECK_FALSE(gate_of(simulator).transmits());
    CHECK(gate_of(simulator).unmet() == timing::NoTx::Pps);
    run_on(simulator, settled_ms + timing::kPpsHoldoverMs + 1010,
           timing::TransmitGate::kNoTxHoldMs);
    CHECK(gate_of(simulator).shown() == timing::NoTx::Pps);

    simulator.world().set_pps_locked(true);
    run_on(simulator, settled_ms + timing::kPpsHoldoverMs + 11020, 2000);
    CHECK(gate_of(simulator).transmits());
    CHECK(gate_of(simulator).shown() == timing::NoTx::None);
}

// A stamp off .000 and .500 names no quarter the receiver solved twice a second on.
TEST_CASE("rf gate: a solution named off the half seconds holds the bursts") {
    Armings armings;
    armings.ready_to_transmit(100);
    armings.tick_at(500);
    REQUIRE(armings.state.transmit.transmits());
    armings.state.own.utc_ms = 250;
    armings.tick_at(510);
    CHECK_FALSE(armings.state.transmit.transmits());
    CHECK(armings.state.transmit.unmet() == timing::NoTx::Gnss);
    armings.state.own.utc_ms = 506;
    armings.tick_at(520);
    CHECK(armings.state.transmit.transmits());
}

TEST_CASE("rf gate: a receiver that lost its rate holds the bursts, and the radio stays judged") {
    Armings armings;
    armings.ready_to_transmit(100);
    armings.tick_at(500);
    REQUIRE(armings.state.transmit.transmits());
    armings.receiver.rate_held = false;
    armings.tick_at(510);
    CHECK_FALSE(armings.state.transmit.transmits());
    CHECK(armings.state.transmit.unmet() == timing::NoTx::Gnss);
}

TEST_CASE("rf gate: an unactivated Go has no address to transmit under, and names it") {
    Armings armings;
    armings.state.activation.serial.reset();
    armings.ready_to_transmit(100);
    armings.tick_at(500);
    CHECK_FALSE(armings.state.transmit.transmits());
    CHECK(armings.state.transmit.unmet() == timing::NoTx::Address);
}

TEST_CASE("rf gate: a solution with no UTC holds the bursts, and names UTC") {
    Armings armings;
    armings.ready_to_transmit(100);
    armings.tick_at(500);
    REQUIRE(armings.state.transmit.transmits());
    armings.state.own.utc_valid = false;
    armings.tick_at(510);
    CHECK_FALSE(armings.state.transmit.transmits());
    CHECK(armings.state.transmit.unmet() == timing::NoTx::Utc);
}

TEST_CASE("rf gate: a radio whose channel reads stop cannot listen before it talks") {
    Armings armings;
    armings.ready_to_transmit(100);
    tick_seconds(armings, 0, 2);
    REQUIRE(armings.state.transmit.transmits());
    armings.rf.deaf = true;
    tick_seconds(armings, 2, 4);
    CHECK_FALSE(armings.state.transmit.transmits());
    CHECK(armings.state.transmit.unmet() == timing::NoTx::Radio);
}

TEST_CASE("rf gate: a radio that enters no dwell is not answering") {
    Armings armings;
    armings.ready_to_transmit(100);
    tick_seconds(armings, 0, 2);
    REQUIRE(armings.state.transmit.transmits());
    armings.rf.stalled = true;
    tick_seconds(armings, 2, 4);
    CHECK_FALSE(armings.state.transmit.transmits());
    CHECK(armings.state.transmit.unmet() == timing::NoTx::Radio);
}

// The radio only listens once the unit has a serial, so the wait before it is not a radio fault.
TEST_CASE("rf gate: an activation after a long wait is no radio fault") {
    Armings armings;
    armings.state.activation.serial.reset();
    armings.ready_to_transmit(100);
    tick_seconds(armings, 0, 12);
    REQUIRE(armings.state.transmit.shown() == timing::NoTx::Address);
    armings.state.activation.serial = 0x7E57;
    armings.tick_in(12, 500);
    CHECK(armings.state.transmit.unmet() == timing::NoTx::None);
    CHECK(armings.state.transmit.shown() == timing::NoTx::None);
}
