// ADS-L 4 SRD-860 issue 2 Subpart C, the physical layer: the bands, the coding and the second's
// slots.
#include <cstdint>
#include <vector>

#include "core/protocol/adsl.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "protocols/common/manchester.h"
#include "simulator/simulator.h"
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

// skyBlip's reading: every transmission the schedule makes alternates channel.
TEST_CASE("ADS-L.4.SRD860.C.2.5: consecutive Traffic transmissions change frequency") {
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
                timing::Scheduler::plan(timing::Scheduler::slot_start(slot), clock);
            const timing::Transmitter::Attempt attempt = tx.attempt(
                plan, utc_s, utc_s * 1000, false, 0, timing::Transmitter::Payload::Position);
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

// C.5 lists Traffic, Status and Remote ID for the direct slot, and every ADS-L burst of ours is
// there.
TEST_CASE("ADS-L.4.SRD860.C.5: own-ship transmits inside the Direct slot and nowhere else") {
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
                    timing::Scheduler::plan(timing::Scheduler::slot_start(slot), clock);
                for (const timing::Transmitter::Payload payload :
                     {timing::Transmitter::Payload::Position,
                      timing::Transmitter::Payload::Status}) {
                    const timing::Transmitter::Attempt owed =
                        tx.attempt(plan, utc_s, 1000, true, 0, payload);
                    if (!owed.go) continue;
                    CHECK(timing::Scheduler::in_direct_slot(owed.at_ms));
                    CHECK(owed.at_ms + static_cast<int>(timing::Transmitter::kAirTimeMs) <=
                          adsl::kDirectEndMs);
                }
            }
        }
    }
}

// The name is skyBlip's payload, not ADS-L's, so it keeps out of the direct slot ADS-L uses.
TEST_CASE("ADS-L.4.SRD860.C.5: the name is ours, in slot 1's tail where ADS-L transmits nothing") {
    timing::ClockState clock{};
    clock.utc_valid = true;
    clock.pps_locked = true;
    const timing::SlotPlan slot1 = timing::Scheduler::plan(timing::kSlot1StartMs, clock);

    int seen = 0;
    for (uint32_t address = 1; address < 64; address++) {
        timing::Transmitter tx;
        tx.configure(address * 0x9E37u);
        for (uint32_t utc_s = 0; utc_s < 40; utc_s++) {
            const timing::Transmitter::Attempt attempt =
                tx.attempt(slot1, utc_s, 1000, true, 0, timing::Transmitter::Payload::Callsign);
            if (!attempt.go) continue;
            seen++;
            CHECK_FALSE(timing::Scheduler::in_direct_slot(attempt.at_ms));
            CHECK(attempt.at_ms >= timing::kCallsignStartMs);
            CHECK(attempt.at_ms + static_cast<int>(timing::Transmitter::kAirTimeMs) <=
                  timing::kCallsignEndMs);
            CHECK(attempt.frequency_hz == adsl::kMBand1Hz);
            CHECK(utc_s % 2 == 0);
        }
    }
    CHECK(seen > 0);
}

TEST_CASE("ADS-L.4.SRD860.C.5: the Uplink slot is listened to on the O band, inside its edges") {
    CHECK(timing::kUplinkRxStartMs >= 200);
    CHECK(timing::kUplinkRxEndMs <= 450);
    for (int phase = timing::kUplinkRxStartMs; phase < timing::kUplinkRxEndMs; phase++) {
        CHECK(timing::Scheduler::band_at(phase) == timing::Band::O);
        CHECK(timing::Scheduler::frequency_hz_at(phase) == adsl::kOBandHz);
    }
    for (int phase = 0; phase < 200; phase++) CHECK_FALSE(timing::Scheduler::in_direct_slot(phase));
}
