// The transmit gate on a flying Go: each requirement, on the radio and receiver behind it.
#include <cstdint>

#include "core/diagnostics/payload.h"
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

// The pulse reads locked kPpsLockTailMs past its last edge, and the gate closes as it stops.
TEST_CASE("rf gate: a PPS lost stops the bursts by the end of its lock tail and reads PPS") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    const uint32_t settled_ms = past_settling(simulator);
    simulator.world().set_pps_locked(false);
    run_on(simulator, settled_ms, timing::kPpsLockTailMs - 2000);
    CHECK(gate_of(simulator).transmits());
    run_on(simulator, settled_ms + timing::kPpsLockTailMs - 1995, 2000);
    CHECK_FALSE(gate_of(simulator).transmits());
    CHECK(gate_of(simulator).unmet() == timing::NoTx::Pps);
    run_on(simulator, settled_ms + timing::kPpsLockTailMs + 10, timing::TransmitGate::kNoTxHoldMs);
    CHECK(gate_of(simulator).shown() == timing::NoTx::Pps);
    CHECK(timing::in_pps_holdover(simulator.product().state().clock));

    simulator.world().set_pps_locked(true);
    run_on(simulator, settled_ms + timing::kPpsLockTailMs + 10020, 2000);
    CHECK(gate_of(simulator).transmits());
    run_on(simulator, settled_ms + timing::kPpsLockTailMs + 12025,
           timing::TransmitGate::kNoTxHoldMs);
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
    CHECK(armings.state.transmit.shown() != timing::NoTx::Radio);
}

namespace {

ports::RfPlan first_with_burst(const Armings& armings) {
    for (uint32_t i = 0; i < armings.rf.handed; i++)
        if (armings.rf.plans[i].tx != nullptr) return armings.rf.plans[i];
    return ports::RfPlan{};
}

}  // namespace

// The bursts stop on the pass a requirement fails, the one already handed to the executor too.
TEST_CASE("rf gate: a burst already armed is withdrawn on the pass the gate closes") {
    Armings armings;
    armings.ready_to_transmit(1000);
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.tick_in(0, 420);
    REQUIRE(armings.rf.with_tx == 1);
    const ports::RfPlan drawn = first_with_burst(armings);

    armings.receiver.rate_held = false;
    armings.tick_in(0, 430);
    CHECK(armings.rf.withdrawn == 1);
    CHECK(armings.rf.withdrawn_at_us == drawn.tx_at_us);
    tick_seconds(armings, 1, 3);
    CHECK(armings.rf.with_tx == 1);
}

// A burst the executor already listens for stays its own, and a busy channel then re-arms nothing.
TEST_CASE("rf gate: a burst found busy after the gate closed is not armed again") {
    Armings armings;
    armings.ready_to_transmit(1000);
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.tick_in(0, 420);
    REQUIRE(armings.rf.with_tx == 1);
    const ports::RfPlan drawn = first_with_burst(armings);

    armings.rf.listening = true;
    armings.receiver.rate_held = false;
    armings.tick_in(0, 430);
    REQUIRE(armings.rf.withdrawn == 0);
    armings.state.rf.note_busy({drawn.tx_at_us, -60, true});
    armings.tick_in(0, static_cast<int>((drawn.tx_at_us - Armings::kEdgeUs) / 1000) + 1);
    CHECK(armings.rf.with_tx == 1);
    CHECK(armings.state.air.tx_deferred == 0);
}

// A turn the gate kept off the air is no turn given up: tx_given_up counts what was owed.
TEST_CASE("rf gate: turns the gate holds back are not counted as given up") {
    Armings armings;
    armings.ready_to_transmit(1000);
    REQUIRE(armings.radio.setup() == Status::Ok);
    tick_seconds(armings, 0, 4);
    const uint32_t given_up = armings.state.air.tx_given_up;
    armings.receiver.rate_held = false;
    tick_seconds(armings, 4, 30);
    CHECK(armings.state.air.tx_given_up == given_up);
}

TEST_CASE("rf gate: a dwell the gate kept quiet records gate_closed, not off schedule") {
    Armings armings;
    armings.recorder.arm();
    armings.ready_to_transmit(1000);
    armings.receiver.rate_held = false;
    REQUIRE(armings.radio.setup() == Status::Ok);
    tick_seconds(armings, 0, 2);

    diagnostics::Record records[diagnostics::Recorder::kMaxRecords];
    const int count = armings.recorder.peek(records, diagnostics::Recorder::kMaxRecords);
    int closed = 0;
    int off_schedule = 0;
    for (int i = 0; i < count; i++) {
        diagnostics::Dwell dwell{};
        if (!diagnostics::read(records[i], dwell)) continue;
        if (dwell.refusal == diagnostics::Refusal::GateClosed) closed++;
        if (dwell.refusal == diagnostics::Refusal::OffSchedule) off_schedule++;
    }
    CHECK(closed > 0);
    CHECK(off_schedule == 0);
}

// A transient SPI timeout costs one dwell, not three seconds of positions.
TEST_CASE(
    "rf gate: dwells refused 3 s apart are transients, two inside 3 s a radio not answering") {
    Armings armings;
    armings.ready_to_transmit(1000);
    tick_seconds(armings, 0, 2);
    REQUIRE(armings.state.transmit.transmits());
    armings.rf.refused_dwells = 1;
    tick_seconds(armings, 2, 1);
    CHECK(armings.state.transmit.transmits());
    tick_seconds(armings, 3, 3);
    CHECK(armings.state.transmit.transmits());
    armings.rf.refused_dwells = 2;
    armings.tick_in(6, 0);
    armings.tick_in(6, 50);
    CHECK(armings.state.transmit.transmits());
    armings.rf.refused_dwells = 3;
    armings.tick_in(6, 100);
    armings.tick_in(6, 150);
    CHECK_FALSE(armings.state.transmit.transmits());
    CHECK(armings.state.transmit.unmet() == timing::NoTx::Radio);
}
