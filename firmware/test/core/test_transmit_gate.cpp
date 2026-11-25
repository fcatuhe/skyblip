// The one verdict every transmit path reads: what ADS-L 4 SRD-860 issue 2 asks before a burst.
#include <cstdint>
#include <string>

#include "core/timing/transmit_gate.h"
#include "doctest/doctest.h"

using namespace skyblip::timing;

namespace {

Requirements all_met() {
    Requirements met{};
    met.address = true;
    met.radio = true;
    met.receiver = true;
    met.fix = true;
    met.settled = true;
    met.utc = true;
    met.pps = true;
    met.positions = true;
    return met;
}

Requirements without(bool Requirements::* requirement) {
    Requirements met = all_met();
    met.*requirement = false;
    return met;
}

// INFO: fc 19nov26 judged every 50 ms from from_ms to to_ms, as RadioService judges every pass
void judge_between(TransmitGate& gate, const Requirements& met, uint32_t from_ms, uint32_t to_ms) {
    for (uint32_t now_ms = from_ms; now_ms <= to_ms; now_ms += 50) gate.judge(met, now_ms);
}

}  // namespace

TEST_CASE("transmit gate: each requirement unmet names its own reason, the first unmet first" *
          doctest::test_suite("main")) {
    CHECK(first_unmet(all_met()) == NoTx::None);
    CHECK(first_unmet(without(&Requirements::address)) == NoTx::Address);
    CHECK(first_unmet(without(&Requirements::radio)) == NoTx::Radio);
    CHECK(first_unmet(without(&Requirements::receiver)) == NoTx::Gnss);
    CHECK(first_unmet(without(&Requirements::fix)) == NoTx::Fix);
    CHECK(first_unmet(without(&Requirements::utc)) == NoTx::Utc);
    CHECK(first_unmet(without(&Requirements::pps)) == NoTx::Pps);
    CHECK(first_unmet(without(&Requirements::positions)) == NoTx::Held);
    CHECK(first_unmet(Requirements{}) == NoTx::Address);
    Requirements deaf_and_lost = without(&Requirements::radio);
    deaf_and_lost.pps = false;
    CHECK(first_unmet(deaf_and_lost) == NoTx::Radio);
}

TEST_CASE("transmit gate: the reasons are one word each, spelled in full") {
    CHECK(std::string(to_string(NoTx::Address)) == "ADDRESS");
    CHECK(std::string(to_string(NoTx::Radio)) == "RADIO");
    CHECK(std::string(to_string(NoTx::Gnss)) == "GNSS");
    CHECK(std::string(to_string(NoTx::Fix)) == "FIX");
    CHECK(std::string(to_string(NoTx::Utc)) == "UTC");
    CHECK(std::string(to_string(NoTx::Pps)) == "PPS");
    CHECK(std::string(to_string(NoTx::Held)) == "HELD");
    CHECK(std::string(to_string(NoTx::None)).empty());
}

// The settle after the first fix holds the burst and is the normal start, never a fault.
TEST_CASE("transmit gate: an unsettled fix transmits nothing and names nothing") {
    TransmitGate gate;
    judge_between(gate, without(&Requirements::settled), 0, 30000);
    CHECK_FALSE(gate.transmits());
    CHECK(gate.unmet() == NoTx::None);
    CHECK(gate.shown() == NoTx::None);
    gate.judge(all_met(), 30050);
    CHECK(gate.transmits());
}

TEST_CASE(
    "transmit gate: a radio that stops answering is shown 10 s on, from boot, without a fix") {
    TransmitGate gate;
    Requirements deaf = without(&Requirements::radio);
    deaf.fix = false;
    gate.judge(deaf, 1000);
    CHECK_FALSE(gate.transmits());
    CHECK(gate.unmet() == NoTx::Radio);
    judge_between(gate, deaf, 1050, 1000 + TransmitGate::kNoTxHoldMs - 50);
    CHECK(gate.shown() == NoTx::None);
    gate.judge(deaf, 1000 + TransmitGate::kNoTxHoldMs);
    CHECK(gate.shown() == NoTx::Radio);
}

TEST_CASE("transmit gate: an unactivated unit says so from boot") {
    TransmitGate gate;
    judge_between(gate, Requirements{}, 0, TransmitGate::kNoTxHoldMs);
    CHECK(gate.shown() == NoTx::Address);
}

// Before the first fix the glass shows its acquisition, which is not a fault.
TEST_CASE("transmit gate: nothing the sky decides is shown before the first fix and its 10 s") {
    TransmitGate gate;
    Requirements acquiring = without(&Requirements::fix);
    acquiring.pps = false;
    judge_between(gate, acquiring, 0, 120000);
    CHECK(gate.unmet() == NoTx::Fix);
    CHECK(gate.shown() == NoTx::None);

    Requirements unpulsed = without(&Requirements::pps);
    judge_between(gate, unpulsed, 120050, 120050 + TransmitGate::kVerdictSettleMs - 50);
    CHECK(gate.shown() == NoTx::None);
    gate.judge(unpulsed, 120050 + TransmitGate::kVerdictSettleMs);
    CHECK(gate.shown() == NoTx::Pps);
}

TEST_CASE("transmit gate: a fix lost in flight stops the burst at once and shows 10 s later" *
          doctest::test_suite("main")) {
    TransmitGate gate;
    judge_between(gate, all_met(), 0, 60000);
    REQUIRE(gate.transmits());

    const Requirements lost = without(&Requirements::fix);
    gate.judge(lost, 60050);
    CHECK_FALSE(gate.transmits());
    CHECK(gate.shown() == NoTx::None);
    judge_between(gate, lost, 60100, 60050 + TransmitGate::kNoTxHoldMs - 50);
    CHECK(gate.shown() == NoTx::None);
    gate.judge(lost, 60050 + TransmitGate::kNoTxHoldMs);
    CHECK(gate.shown() == NoTx::Fix);

    gate.judge(all_met(), 80000);
    CHECK(gate.transmits());
    CHECK(gate.shown() == NoTx::Fix);
    judge_between(gate, all_met(), 80050, 80000 + TransmitGate::kNoTxHoldMs - 50);
    CHECK(gate.shown() == NoTx::Fix);
    gate.judge(all_met(), 80000 + TransmitGate::kNoTxHoldMs);
    CHECK(gate.shown() == NoTx::None);
}

// A requirement that fails and holds again in turn would otherwise redraw the mark every 10 s.
TEST_CASE("transmit gate: a mark stays through a short recovery, and names the new failure") {
    TransmitGate gate;
    judge_between(gate, all_met(), 0, 60000);
    judge_between(gate, without(&Requirements::pps), 60050, 70050);
    REQUIRE(gate.shown() == NoTx::Pps);
    judge_between(gate, all_met(), 70100, 75000);
    CHECK(gate.transmits());
    CHECK(gate.shown() == NoTx::Pps);
    gate.judge(without(&Requirements::fix), 75050);
    CHECK_FALSE(gate.transmits());
    CHECK(gate.shown() == NoTx::Fix);
}

TEST_CASE("transmit gate: a gap shorter than the hold never shows, and a new gap starts afresh") {
    TransmitGate gate;
    judge_between(gate, all_met(), 0, 60000);
    const Requirements gap = without(&Requirements::pps);
    judge_between(gate, gap, 60050, 69000);
    gate.judge(all_met(), 69050);
    judge_between(gate, gap, 69100, 75000);
    CHECK_FALSE(gate.transmits());
    CHECK(gate.shown() == NoTx::None);
}

TEST_CASE("transmit gate: a failure that changes reason keeps its hold and names the new one") {
    TransmitGate gate;
    judge_between(gate, all_met(), 0, 60000);
    judge_between(gate, without(&Requirements::pps), 60050, 65000);
    judge_between(gate, without(&Requirements::fix), 65050, 70050);
    CHECK(gate.shown() == NoTx::Fix);
}

// The gate stays open on held positions, so the bursts resume the moment a fix dates them again.
TEST_CASE("transmit gate: positions held name HELD, shown 10 s later, and the gate stays open" *
          doctest::test_suite("main")) {
    TransmitGate gate;
    judge_between(gate, all_met(), 0, 20000);
    REQUIRE(gate.on_air());

    const Requirements held = without(&Requirements::positions);
    judge_between(gate, held, 20050, 30000);
    CHECK(gate.transmits());
    CHECK_FALSE(gate.on_air());
    CHECK(gate.unmet() == NoTx::Held);
    CHECK(gate.shown() == NoTx::None);
    judge_between(gate, held, 30050, 30100);
    CHECK(gate.shown() == NoTx::Held);
}
