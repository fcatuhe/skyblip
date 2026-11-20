// ADS-L 4 SRD-860 issue 2 Subpart C, the physical layer: the bands, the coding and the second's
// slots.
#include <cstdint>
#include <vector>

#include "core/protocol/adsl.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "protocols/common/manchester.h"
#include "simulator/simulator.h"
#include "test/support/fresh_fix.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {
uint16_t chips_of(uint8_t byte) {
    uint8_t coded[2] = {0, 0};
    wire::manchester_encode(&byte, 1, coded);
    return static_cast<uint16_t>((coded[0] << 8) | coded[1]);
}

}  // namespace

// The component sets the ceiling, and the conducted power and antenna this product declares meet
// it.
TEST_CASE("ADS-L.4.SRD860.C.2: the transmitter stays under the M band's 14 dBm e.r.p.") {
    CHECK(parts::sx1262::kResultingErpCentiDb <= adsl::kMBandErpCeilingDbm * 100);
}

// The Go holds the channel busy at a burst's instant and measures the wait to its next burst.
TEST_CASE("ADS-L.4.SRD860.C.2: a busy burst backs off 15 to 250 ms, and stays in the direct slot") {
    const int8_t held_dbm[1] = {-50};
    for (const uint32_t held_after_ms :
         {2000u, 2370u, 3150u, 4730u, 6290u, 7110u, 8520u, 9940u, 11380u, 12650u}) {
        CAPTURE(held_after_ms);
        simulator::Simulator simulator;
        REQUIRE(simulator.setup() == Status::Ok);
        simulator.world().set_fix(true);
        simulator.world().set_speed_kt(50);
        uint32_t now_ms = past_settling(simulator);
        run_on(simulator, now_ms + 1, held_after_ms, 1);
        now_ms += held_after_ms;
        const bus::State& state = simulator.product().state();
        simulator.platform().chips().radio.set_rssi_sequence(held_dbm, 1);
        const uint32_t until_ms = now_ms + 30000;
        while (state.air.tx_deferred == 0 && now_ms < until_ms) simulator.step(++now_ms);
        simulator.platform().chips().radio.set_rssi_sequence(held_dbm, 0);
        REQUIRE(state.air.tx_deferred == 1);
        const uint64_t busy_at_us = state.rf.busy_at(state.rf.busy_reports - 1)->tx_at_us;
        run_on(simulator, now_ms + 1, 1000, 1);

        uint64_t next_at_us = 0;
        const simulator::Air& air = simulator.world().air();
        for (int i = 0; i < air.record_count() && next_at_us == 0; i++)
            if (air.record_at(i).event == simulator::AirEvent::Tx &&
                air.record_at(i).at_us > busy_at_us)
                next_at_us = air.record_at(i).at_us;
        REQUIRE(next_at_us != 0);
        CHECK(next_at_us - busy_at_us >= adsl::kMBandBackoff.floor_us);
        CHECK(next_at_us - busy_at_us <= adsl::kMBandBackoff.ceiling_us);
        const int phase_ms = static_cast<int>(next_at_us / 1000 % 1000);
        CHECK(phase_ms >= adsl::kDirectStartMs);
        CHECK(phase_ms + static_cast<int>(adsl::kAirTimeMs) <= adsl::kDirectEndMs);
    }
    CHECK(timing::PoliteAccess::kDeferralFloorUs == adsl::kMBandBackoff.floor_us);
    CHECK(timing::PoliteAccess::kDeferralCeilingUs == adsl::kMBandBackoff.ceiling_us);
}

// The radio sends eight repetitions; the last four and both 1001s are the leading sync byte.
TEST_CASE("ADS-L.4.SRD860.C.2.3: the preamble is at most twelve 01s and then two 1001s") {
    const uint8_t lead = static_cast<uint8_t>(adsl::kSyncWord >> 24);
    CHECK(chips_of(lead) == 0x5599);

    const int radio_repetitions = parts::sx1262::kPreambleChips / 2;
    const int lead_repetitions = 4;
    CHECK(radio_repetitions == 8);
    CHECK(radio_repetitions + lead_repetitions >= 5);
    CHECK(radio_repetitions + lead_repetitions <= 12);
}

// skyBlip's reading: within a flight state, every transmission the schedule makes alternates.
TEST_CASE("ADS-L.4.SRD860.C.2.5: consecutive Traffic transmissions change frequency" *
          doctest::test_suite("main")) {
    uint32_t previous = 0;
    for (uint32_t utc_s = 1785628800; utc_s < 1785628810; utc_s++) {
        const int slot = timing::Transmitter::slot_in(utc_s);
        const uint32_t frequency = timing::Scheduler::slot_frequency_hz(slot);
        CHECK((frequency == adsl::kMBand0Hz || frequency == adsl::kMBand1Hz));
        if (previous != 0) CHECK(frequency != previous);
        previous = frequency;
    }
}

TEST_CASE("ADS-L.4.SRD860.C.2.5: consecutive Status transmissions change frequency") {
    for (const int speed_kt : {0, 50}) {
        CAPTURE(speed_kt);
        const std::vector<simulator::AirRecord> statuses = statuses_flown(speed_kt, 61);
        CHECK(statuses.size() >= 3);
        for (size_t i = 1; i < statuses.size(); i++)
            CHECK(statuses[i].frequency_hz != statuses[i - 1].frequency_hz);
    }
}

// A ground position that missed +0 goes on A at +2, and the next one, at +9, is still on B.
TEST_CASE(
    "ADS-L.4.SRD860.C.2.5: a burst that never went out does not move the next one's channel") {
    timing::ClockState clock{};
    clock.utc_valid = true;
    clock.pps_locked = true;
    timing::Transmitter tx;
    tx.configure(1);
    const uint32_t c = 2000 + tx.cycle_start();
    std::vector<int> slots;
    for (uint32_t utc_s = c; utc_s < c + 20; utc_s++) {
        tx.give_up(utc_s, false, true);
        for (int slot = 0; slot < 2; slot++) {
            const timing::SlotPlan plan =
                timing::Scheduler::plan(timing::Scheduler::window(slot).start_ms, clock);
            const timing::Transmitter::Attempt attempt =
                tx.attempt(plan, utc_s, utc_s * 1000, false, kFixSolvedAtSecondEnd,
                           timing::Transmitter::Payload::Position);
            if (!attempt.go || utc_s == c) continue;
            tx.sent(utc_s, utc_s * 1000, timing::Transmitter::Payload::Position);
            slots.push_back(slot);
        }
    }
    CHECK(slots == std::vector<int>{0, 1});
}

// Airborne the position takes one channel each second, and the Status takes the other.
TEST_CASE("ADS-L.4.SRD860.C.2.5: a Status flies on the channel the second's position leaves free") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    run_on(simulator, past_settling(simulator), 41000);

    const simulator::Air& air = simulator.world().air();
    int checked = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& status = air.record_at(i);
        if (payload_type_of(status) != adsl::Packet::kPayloadTypeStatus) continue;
        const uint64_t second = (status.at_us - status.phase_ms * 1000ull) / 1000000;
        for (int j = 0; j < air.record_count(); j++) {
            const simulator::AirRecord& position = air.record_at(j);
            if (payload_type_of(position) != adsl::Packet::kPayloadTypePosition ||
                (position.at_us - position.phase_ms * 1000ull) / 1000000 != second)
                continue;
            CHECK(position.frequency_hz != status.frequency_hz);
            checked++;
        }
    }
    CHECK(checked >= 3);
}

// Payload 66 goes out after the direct slot, which docs/ADSL_EASA_QUESTIONS.md asks EASA about.
TEST_CASE("ADS-L.4.SRD860.C.5: own-ship's Traffic and Status go out inside the Direct slot" *
          doctest::test_suite("main")) {
    CHECK(adsl::kDirectStartMs == 450);
    CHECK(adsl::kDirectEndMs == 1000);

    timing::ClockState clock{};
    clock.utc_valid = true;
    clock.pps_locked = true;

    for (uint32_t address = 1; address < 64; address++) {
        timing::Transmitter tx;
        tx.configure(address * 0x9E37u);
        for (uint32_t utc_s = 0; utc_s < 40; utc_s++) {
            for (int slot = 0; slot < 2; slot++) {
                const timing::SlotPlan plan =
                    timing::Scheduler::plan(timing::Scheduler::window(slot).start_ms, clock);
                for (const timing::Transmitter::Payload payload :
                     {timing::Transmitter::Payload::Position,
                      timing::Transmitter::Payload::Status}) {
                    const timing::Transmitter::Attempt owed =
                        tx.attempt(plan, utc_s, 1000, true, kFixSolvedAtSecondEnd, payload);
                    if (!owed.go) continue;
                    CHECK(owed.at_ms >= adsl::kDirectStartMs);
                    CHECK(owed.at_ms + static_cast<int>(timing::Transmitter::kAirTimeMs) <=
                          adsl::kDirectEndMs);
                }
            }
        }
    }
}

TEST_CASE("ADS-L.4.SRD860.C.5: the Uplink slot is listened to on the O band, inside its edges") {
    const timing::Window dwell = timing::dwell_of(timing::kUplinkWindowMs);
    CHECK(dwell.start_ms >= adsl::kUplinkSlotStartMs);
    CHECK(dwell.end_ms <= adsl::kUplinkSlotEndMs);
    for (int phase = dwell.start_ms; phase < dwell.end_ms; phase++) {
        const timing::SlotPlan plan = timing::Scheduler::plan(phase, timing::ClockState{});
        CHECK(plan.band == timing::Band::O);
        CHECK(plan.frequency_hz == adsl::kOBandHz);
    }
    for (int phase = 0; phase < 200; phase++) CHECK_FALSE(timing::Scheduler::in_direct_slot(phase));
}
