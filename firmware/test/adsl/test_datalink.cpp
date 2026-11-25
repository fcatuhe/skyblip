// ADS-L 4 SRD-860 issue 2 Subpart D, the data link: media access as flown.
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/air.h"
#include "core/radio/log.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "protocols/adsl/media_access.h"
#include "protocols/adsl/physical.h"
#include "simulator/simulator.h"
#include "test/support/armings_rig.h"
#include "test/support/m_band_rig.h"
#include "test/support/product_rig.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

// Own-ship's bursts with its channel held busy for `held_ms` from the top of a cycle.
std::vector<simulator::AirRecord> held_from_cycle_start(int speed_kt, uint32_t held_ms,
                                                        uint64_t& held_from_us, uint32_t& forced) {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    std::strncpy(simulator.product().settings().callsign, "D-KXYZ", go::kCallsignBytes - 1);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(speed_kt);
    uint32_t now_ms = past_settling(simulator);
    const timing::Transmitter& transmitter = simulator.product().radio().transmitter();
    const auto cycle_second = [&]() {
        const uint32_t second = simulator.product().state().traffic_now_s(now_ms);
        return (second + timing::Transmitter::kCycleS - transmitter.cycle_start()) %
               timing::Transmitter::kCycleS;
    };
    const uint32_t ceiling_ms = now_ms + 30000;
    while (now_ms < ceiling_ms && !(now_ms % 1000 == 0 && cycle_second() == 0))
        simulator.step(++now_ms);
    REQUIRE(now_ms % 1000 == 0);

    held_from_us = static_cast<uint64_t>(now_ms) * 1000;
    const int8_t held_dbm[1] = {-50};
    models::Sx1262& chip = simulator.platform().chips().radio;
    chip.set_rssi_sequence(held_dbm, 1);
    run_on(simulator, now_ms + 1, held_ms - 1, 1);
    chip.set_rssi_sequence(held_dbm, 0);
    forced = simulator.product().state().air.tx_forced;

    std::vector<simulator::AirRecord> sent;
    const simulator::Air& air = simulator.world().air();
    for (int i = 0; i < air.record_count(); i++)
        if (air.record_at(i).event == simulator::AirEvent::Tx &&
            air.record_at(i).at_us >= held_from_us)
            sent.push_back(air.record_at(i));
    return sent;
}

uint64_t last_armed_burst(const Armings& armings) {
    for (uint32_t i = armings.rf.handed; i > 0; --i)
        if (armings.rf.plans[i - 1].tx != nullptr) return armings.rf.plans[i - 1].tx_at_us;
    return 0;
}

}  // namespace

TEST_CASE(
    "ADS-L.4.SRD860.D.3: an early busy report with no backoff left closes the dwell's burst") {
    Armings armings;
    armings.state.activation.serial = 3;
    armings.ready_to_transmit(1000);
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.tick_at(420);
    REQUIRE(armings.rf.with_tx == 1);
    const uint64_t drawn_us = last_armed_burst(armings);
    const int phase_ms = static_cast<int>((drawn_us - Armings::kEdgeUs) / 1000);
    REQUIRE(phase_ms > 810 - 15);
    REQUIRE(phase_ms <= 810);

    armings.state.rf.note_busy({drawn_us, -50, true});
    armings.tick_at(phase_ms - 1);
    armings.tick_at(phase_ms);
    armings.tick_at(821);

    CHECK(armings.state.air.tx_busy_missed == 1);
    CHECK(armings.rf.with_tx == 1);
}

TEST_CASE("ADS-L.4.SRD860.D.3: a late fix cannot cut short a selected backoff") {
    Armings armings;
    armings.state.activation.serial = 22;
    armings.ready_to_transmit(1000);
    armings.solving = false;
    const uint32_t edge_ms = Armings::kEdgeUs / 1000;
    armings.state.own.fix_at_ms = edge_ms;
    armings.state.own.fix_in_hand_ms = edge_ms + 70;
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.tick_at(420);
    REQUIRE(armings.rf.with_tx == 1);
    const uint64_t drawn_us = last_armed_burst(armings);
    REQUIRE(drawn_us == Armings::kEdgeUs + 476000);
    // This serial draws 40 units of 5 ms at .476, so the backoff ends at .676.
    const uint64_t not_before_us = drawn_us + 200000;
    armings.state.rf.note_busy({drawn_us, -50, true});
    armings.tick_at(477);
    CHECK(armings.state.air.tx_deferred == 1);
    CHECK(armings.rf.with_tx == 1);

    // A fix in hand at .540 redraws to .595, inside the backoff, so only the clamp keeps .676.
    armings.state.own.utc_ms = 500;
    armings.state.own.fix_at_ms = edge_ms + 500;
    armings.state.own.fix_in_hand_ms = edge_ms + 540;
    armings.tick_at(540);

    CHECK(armings.rf.with_tx == 2);
    CHECK(last_armed_burst(armings) >= not_before_us);
    CHECK(last_armed_burst(armings) <= Armings::kEdgeUs + 810000);
}

// EN 300 220-1 5.21.1: the random interval runs from the assessment, which can come after the plan.
TEST_CASE("ADS-L.4.SRD860.D.3: a channel heard busy late defers from when it was heard") {
    Armings armings;
    armings.state.activation.serial = 1;
    armings.ready_to_transmit(1000);
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.tick_at(420);
    REQUIRE(armings.rf.with_tx == 1);
    const uint64_t drawn_us = last_armed_burst(armings);
    const int phase_ms = static_cast<int>((drawn_us - Armings::kEdgeUs) / 1000);
    REQUIRE(phase_ms < 650);

    const uint64_t heard_us = drawn_us + 20000;
    armings.state.rf.note_busy({drawn_us, -50, true, heard_us});
    armings.tick_at(phase_ms + 21);

    CHECK(armings.state.air.tx_deferred == 1);
    REQUIRE(armings.rf.with_tx == 2);
    CHECK(last_armed_burst(armings) >= heard_us + timing::PoliteAccess::kDeferralFloorUs);
}

// Forced on the ground only: airborne the Status and the name are still busy at +8, past 3000 ms.
TEST_CASE("ADS-L.4.SRD860.D.3: a held channel defers, forces after 3000 ms, then keeps 2000 ms" *
          doctest::test_suite("main")) {
    uint64_t held_from_us = 0;
    uint32_t forced = 0;
    const std::vector<simulator::AirRecord> ground =
        held_from_cycle_start(0, 12000, held_from_us, forced);
    CHECK(forced >= 2);
    REQUIRE(ground.size() >= 2);
    CHECK(ground.size() <= forced);
    if (!ground.empty()) CHECK(ground[0].at_us - held_from_us >= 3000000);
    const uint64_t burst_us = 4640;
    for (size_t i = 1; i < ground.size(); i++)
        CHECK(ground[i].at_us - ground[i - 1].at_us >= burst_us + 2000000);

    const std::vector<simulator::AirRecord> airborne_past_3000_ms =
        held_from_cycle_start(50, 9500, held_from_us, forced);
    CHECK(airborne_past_3000_ms.empty());
    CHECK(forced == 0);
}

// D.1.1's length byte is the last of the sync tail, so a receiver reads each burst's own length.
TEST_CASE("ADS-L.4.SRD860.D.1.1: the Go sends a Status as 16 bytes and Traffic as 24") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    run_on(simulator, past_settling(simulator), 21000);
    int statuses = 0;
    int positions = 0;
    const simulator::Air& air = simulator.world().air();
    for (int i = 0; i < air.record_count(); i++) {
        adsl::Packet packet{};
        if (!packet_of(air.record_at(i), packet)) continue;
        protocol::Frame frame{};
        REQUIRE(simulator::Air::framed(air.record_at(i), frame));
        if (packet.payload_type == adsl::Packet::kPayloadTypeStatus) {
            statuses++;
            CHECK(frame.bytes == 16);
        }
        if (packet.is_position()) {
            positions++;
            CHECK(frame.bytes == 24);
        }
    }
    CHECK(statuses >= 1);
    CHECK(positions >= 20);
}

// G.2 makes a Status 16 bytes, so one padded to Traffic's 24 is not read as one.
TEST_CASE("ADS-L.4.SRD860.D.1.1: a Status padded to 24 bytes is refused, never filed") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    fly(rig, now_ms, 3);
    model::Ownship neighbour = rig.state().own;
    neighbour.latitude_1e7 += 10000;
    adsl::Packet traffic{};
    REQUIRE(protocol::from_own(traffic, neighbour, 0xC5D804, adsl::kAddressTableFlarm, 4));
    hear_adsl(rig, traffic);
    settle(rig, now_ms);

    const uint8_t padded_status[] = {0x61, 0x03, 0x06, 0x01, 0x76, 0x31, 0x01, 0x00,
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0xF4, 0xBA, 0x2F};
    hear_m_band(rig, 0xF5724B18u, padded_status, sizeof(padded_status));
    settle(rig, now_ms);
    CHECK(heard_verdict(rig) == radio::Verdict::Unsupported);
    CHECK(rig.state().air.rx_status == 0);
    const int index = rig.state().traffic.find(adsl::kAddressTableFlarm, 0xC5D804);
    REQUIRE(index >= 0);
    CHECK_FALSE(rig.state().traffic.at(index)->version.declared);
}

TEST_CASE("ADS-L.4.SRD860.D.1.1: the Go trusts a clean length and repairs only a doubtful one") {
    const uint8_t traffic[] = {0x00, 0xD1, 0x87, 0x13, 0x91, 0x5C, 0x80, 0x2C,
                               0x88, 0xD4, 0xD3, 0x86, 0x0E, 0x87, 0xD1, 0x04,
                               0x40, 0xC5, 0x3C, 0x02, 0x7F, 0x88, 0x7D, 0xBA};
    struct Trial {
        uint8_t length;
        uint8_t flipped_chips;
        bool accepted;
    };
    const Trial trials[] = {
        {0, 0, false},      {16, 0, false},   {24, 0, true},     {25, 0, false},   {0, 0x02, false},
        {16, 0x02, false},  {24, 0x02, true}, {25, 0x02, true},  {16, 0x80, true}, {24, 0x80, true},
        {255, 0x02, false}, {25, 0x0A, true}, {24, 0x2A, false},
    };
    for (const auto& [length, flipped_chips, accepted] : trials) {
        CAPTURE(int(length));
        CAPTURE(int(flipped_chips));
        Rig rig;
        REQUIRE(rig.setup() == Status::Ok);
        uint32_t now_ms = 0;
        fly(rig, now_ms, 3);

        events::RfEvent event{};
        event.type = events::RfEventType::RxDone;
        event.band = model::Band::M;
        event.rssi_dbm = -15;
        event.rssi_valid = true;
        event.bytes = protocol::kRxChipBytes;
        const auto put_byte = [&](size_t offset, uint8_t value) {
            uint16_t chips = 0;
            for (int bit = 7; bit >= 0; --bit)
                chips = static_cast<uint16_t>((chips << 2) | ((value >> bit) & 1 ? 1 : 2));
            event.data[offset * 2] = static_cast<uint8_t>(chips >> 8);
            event.data[offset * 2 + 1] = static_cast<uint8_t>(chips);
        };
        put_byte(0, 0x72);
        put_byte(1, 0x4B);
        put_byte(2, length);
        for (size_t i = 0; i < sizeof(traffic); i++) put_byte(3 + i, traffic[i]);
        event.data[5] ^= flipped_chips;
        rig.product.bus().rf.push(event);
        settle(rig, now_ms);

        CHECK((heard_verdict(rig) == radio::Verdict::Received) == accepted);
        CHECK(rig.state().air.rx_ok == (accepted ? 1u : 0u));
    }
}
