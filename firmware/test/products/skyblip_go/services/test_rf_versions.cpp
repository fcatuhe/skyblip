// ADS-L B.5 at the radio: a close client heard after a position is armed reaches it before it keys.
#include <cstdint>
#include <cstring>

#include "core/model/aircraft.h"
#include "core/traffic/versions.h"
#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"
#include "simulator/world/air.h"
#include "test/support/armings_rig.h"
#include "test/support/simulator_run.h"
#include "test/support/traffic_scene.h"

using namespace skyblip;

namespace {

constexpr uint32_t kUtcS = 1000;
constexpr uint8_t kHeavyRotorcraft = 15;

// A heavy helicopter, which goes out in Issue 2 unless a close client reads only Issue 1.
void ready(Armings& armings) {
    armings.ready_to_transmit(kUtcS);
    model::Ownship& own = armings.state.own;
    own.latitude_1e7 = 481000000;
    own.longitude_1e7 = 81000000;
    own.altitude_hae_mm = 1000000;
    own.speed_mm_s = 30000;
    own.hdop_e2 = 90;
    own.vdop_e2 = 120;
    own.climb_valid = true;
    own.aircraft_category = kHeavyRotorcraft;
}

const ports::RfPlan* last_with_burst(const Armings& armings) {
    const ports::RfPlan* found = nullptr;
    for (uint32_t i = 0; i < armings.rf.handed; i++)
        if (armings.rf.plans[i].tx != nullptr) found = &armings.rf.plans[i];
    return found;
}

// The radio puts its sync word in front of the chips it was handed, as the model does.
adsl::Packet packet_in(const ports::RfPlan& plan) {
    simulator::AirRecord record{};
    record.event = simulator::AirEvent::Tx;
    const uint8_t sync_bytes = plan.sync_bits / 8;
    std::memcpy(record.chips, plan.sync, sync_bytes);
    std::memcpy(record.chips + sync_bytes, plan.tx, plan.tx_bytes);
    record.bytes = static_cast<uint16_t>(sync_bytes + plan.tx_bytes);
    adsl::Packet packet{};
    REQUIRE(packet_of(record, packet));
    return packet;
}

// An Issue 1 client 300 m north at our height, heard direct on this pass.
void hear_issue1_client(Armings& armings) {
    const model::Ownship& own = armings.state.own;
    model::AircraftObservation client = target_at(own, 300, 0, 0, 30, 90, own.fix_at_ms);
    client.adsl_version = adsl::Packet::kProtocolVersionIssue1;
    client.source = model::Source::AdslDirect;
    client.received.at_s = kUtcS;
    armings.versions.hear_traffic(client, kUtcS);
}

}  // namespace

TEST_CASE("rf: a close Issue 1 client heard after arming rewrites the position at its instant" *
          doctest::test_suite("main")) {
    Armings armings;
    ready(armings);
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.tick_in(0, 420);
    REQUIRE(armings.rf.with_tx == 1);
    const ports::RfPlan armed = *last_with_burst(armings);
    REQUIRE(int(packet_in(armed).protocol_version()) == adsl::Packet::kProtocolVersionIssue2);

    hear_issue1_client(armings);
    armings.tick_in(0, 430);
    CHECK(armings.rf.withdrawn == 1);
    CHECK(armings.rf.withdrawn_at_us == armed.tx_at_us);
    REQUIRE(armings.rf.with_tx == 2);
    const ports::RfPlan rewritten = *last_with_burst(armings);
    CHECK(rewritten.tx_at_us == armed.tx_at_us);
    CHECK_FALSE(rewritten.tx_forced);
    const adsl::Packet sent = packet_in(rewritten);
    CHECK(int(sent.protocol_version()) == adsl::Packet::kProtocolVersionIssue1);
    CHECK(int(sent.aircraft_category) == 3);

    armings.tick_in(0, 440);
    CHECK(armings.rf.withdrawn == 1);
    CHECK(armings.rf.with_tx == 2);
}

// Issue 1 has no code for an unknown climb, so the position is taken back and held.
TEST_CASE("rf: a position Issue 1 cannot write is withdrawn and held once the client is heard") {
    Armings armings;
    ready(armings);
    armings.state.own.climb_valid = false;
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.tick_in(0, 420);
    REQUIRE(armings.rf.with_tx == 1);
    const uint32_t held = armings.state.air.tx_held_for_b5;

    hear_issue1_client(armings);
    armings.tick_in(0, 430);
    CHECK(armings.rf.withdrawn == 1);
    CHECK(armings.rf.with_tx == 1);
    CHECK(armings.state.air.tx_held_for_b5 == held + 1);
}

// Once its channel check has begun a burst is the executor's, so it keys as armed.
TEST_CASE("rf: a position the executor already listens for keys as it was armed") {
    Armings armings;
    ready(armings);
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.tick_in(0, 420);
    REQUIRE(armings.rf.with_tx == 1);

    armings.rf.listening = true;
    hear_issue1_client(armings);
    armings.tick_in(0, 430);
    CHECK(armings.rf.withdrawn == 0);
    CHECK(armings.rf.with_tx == 1);
}

// The device's loop sleeps until something is due, so the radio asks for a pass in time to reread.
TEST_CASE("rf: a carried position brings a pass ahead of its channel check") {
    Armings armings;
    ready(armings);
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.tick_in(0, 420);
    REQUIRE(armings.rf.with_tx == 1);
    const uint64_t tx_at_us = last_with_burst(armings)->tx_at_us;
    const int opens_ms = timing::kWindowAMs.start_ms - timing::kSwitchLeadMs;
    REQUIRE(tx_at_us > Armings::kEdgeUs + static_cast<uint64_t>(opens_ms) * 1000 +
                           go::RadioService::kVersionRereadLeadUs);

    armings.tick_in(0, opens_ms);
    const uint64_t now_us = armings.clock.microseconds();
    const uint64_t due_us =
        now_us + uint64_t{armings.radio.due_in_ms(static_cast<uint32_t>(now_us / 1000))} * 1000;
    CHECK(due_us <= tx_at_us - go::RadioService::kVersionRereadLeadUs + 1000);
    CHECK(due_us + timing::PoliteAccess::kCcaLeadUs < tx_at_us);
}
