// ADS-L 4 SRD-860 issue 2 B.5 over the bursts a simulated Go flies beside other clients.
#include <cstdint>
#include <optional>
#include <vector>

#include "core/bus/state.h"
#include "core/events/rf.h"
#include "core/model/band.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/air.h"
#include "core/settings/address.h"
#include "core/timing/slot.h"
#include "core/timing/transmit_gate.h"
#include "core/traffic/table.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/platform/host/identity.h"
#include "protocols/adsl/flight_state.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/physical.h"
#include "protocols/common/manchester.h"
#include "simulator/simulator.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

constexpr uint32_t kNeighbour = 0xC5D804;
constexpr uint8_t kHeavyRotorcraft = 15;

}  // namespace

namespace {

struct Flown {
    int sent{0};
    uint32_t held{0};
    uint32_t given_up{0};
    uint8_t version{0};
    uint8_t category{0};
    timing::NoTx unmet{timing::NoTx::None};
};

// Every simulated aircraft sends version 0, so to a Go it is an Issue 1 client until a Status says
// more. A VDOP of 0 is a 2D fix, whose altitude goes out as unknown.
Flown flown(uint8_t category, bool beside_issue1, uint16_t vdop_e2 = 150, int speed_kt = 60,
            uint32_t for_ms = 15000, bool speed_reported = true) {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.product().settings().aircraft_type = category;
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(speed_kt);
    simulator.world().gnss().vdop_e2 = vdop_e2;
    if (beside_issue1) simulator.world().add_aircraft(0, 400, 0, speed_kt == 0 ? 1 : 30, 90);
    uint32_t settled_ms = past_settling(simulator);
    if (!speed_reported) {
        simulator.world().gnss().emit_speed = false;
        run_on(simulator, settled_ms, 1000);
        simulator.world().air().clear();
        settled_ms += 1000 + simulator::Simulator::kStepMs;
    }
    const uint32_t held_before = simulator.product().state().air.tx_held_for_b5;
    const uint32_t given_up_before = simulator.product().state().air.tx_given_up;
    run_on(simulator, settled_ms, for_ms);

    Flown out;
    for (const simulator::AirRecord& record :
         records_of(simulator.world().air(), adsl::Packet::kPayloadTypePosition)) {
        adsl::Packet packet{};
        REQUIRE(packet_of(record, packet));
        if (packet.address() != settings::address_of(platform::host::Identity::kSerial)) continue;
        if (out.sent > 0) {
            CHECK(packet.protocol_version() == out.version);
            CHECK(packet.aircraft_category == out.category);
        }
        out.version = packet.protocol_version();
        out.category = packet.aircraft_category;
        out.sent++;
    }
    out.held = simulator.product().state().air.tx_held_for_b5 - held_before;
    out.given_up = simulator.product().state().air.tx_given_up - given_up_before;
    out.unmet = simulator.product().state().transmit.unmet();
    return out;
}

}  // namespace

// Through RadioService: category 15 goes as Issue 1's rotorcraft beside an Issue 1 client, alone as
// itself.
TEST_CASE("ADS-L.4.SRD860.B.5: the version transmitted is one every close neighbour can read" *
          doctest::test_suite("main")) {
    const Flown beside = flown(kHeavyRotorcraft, true);
    CHECK(beside.sent > 10);
    CHECK(beside.held == 0);
    CHECK(beside.version == adsl::Packet::kProtocolVersionIssue1);
    CHECK(int(beside.category) == 3);

    const Flown alone = flown(kHeavyRotorcraft, false);
    CHECK(alone.sent > 10);
    CHECK(alone.version == adsl::Packet::kProtocolVersionIssue2);
    CHECK(int(alone.category) == kHeavyRotorcraft);
}

// Issue 1 has no code for an unknown altitude, and we never claim one we lack.
TEST_CASE("ADS-L.4.SRD860.B.5: a 2D fix beside an Issue 1 client puts no position on air") {
    const Flown beside = flown(4, true, /*vdop_e2=*/0);
    CHECK(beside.sent == 0);
    CHECK(beside.held > 10);
    CHECK(beside.unmet == timing::NoTx::Held);

    const Flown alone = flown(4, false, /*vdop_e2=*/0);
    CHECK(alone.sent > 10);
    CHECK(alone.held == 0);
    CHECK(alone.version == adsl::Packet::kProtocolVersionIssue2);
    CHECK(alone.unmet == timing::NoTx::None);
}

// Issue 1 has no code for an unknown speed either, and the Go sent 0 m/s in its place.
TEST_CASE(
    "ADS-L.4.SRD860.B.5: a fix with no ground speed beside an Issue 1 client puts none on air") {
    const Flown beside = flown(4, true, 150, 60, 15000, /*speed_reported=*/false);
    CHECK(beside.sent == 0);
    CHECK(beside.held > 10);

    const Flown alone = flown(4, false, 150, 60, 15000, /*speed_reported=*/false);
    CHECK(alone.sent > 10);
    CHECK(alone.held == 0);
    CHECK(alone.version == adsl::Packet::kProtocolVersionIssue2);
}

TEST_CASE("ADS-L.4.SRD860.B.5: beside an Issue 1 client a glider still goes out every second") {
    const Flown glider = flown(4, true);
    CHECK(glider.sent > 10);
    CHECK(glider.held == 0);
    CHECK(glider.version == adsl::Packet::kProtocolVersionIssue1);
    CHECK(int(glider.category) == 4);
}

// On the ground a held position tries again every 2 s, +2 to +8 and +11 to +19, then gives up.
TEST_CASE("ADS-L.4.SRD860.B.5: a held ground position is retried 2 s on until its next turn") {
    const Flown parked = flown(4, true, /*vdop_e2=*/0, /*speed_kt=*/0, /*for_ms=*/40000);
    CHECK(parked.sent == 0);
    CHECK(parked.held >= 15);
    CHECK(parked.given_up >= 3);
}

namespace {

constexpr uint32_t kFarClientBase = 0x3A0000;
constexpr int kFarClients = 48;
constexpr int32_t kCloseNorthM = 300;
constexpr int32_t kFarNorthM = 20000;

struct OwnPosition {
    uint64_t at_us{0};
    uint8_t version{0};
    uint8_t category{0};
};

// A heavy helicopter flown in the simulator: its category on air says which issue wrote it, 15 in
// Issue 2 and 3, Issue 1's rotorcraft, in Issue 1.
struct Flight {
    simulator::Simulator simulator;
    uint32_t now_ms{0};
    uint32_t step_ms{simulator::Simulator::kStepMs};

    explicit Flight(uint32_t step = simulator::Simulator::kStepMs) : step_ms(step) {
        REQUIRE(simulator.setup() == Status::Ok);
        simulator.product().settings().aircraft_type = kHeavyRotorcraft;
        simulator.world().set_fix(true);
        simulator.world().set_speed_kt(60);
        now_ms = past_settling(simulator);
    }

    const bus::State& state() { return simulator.product().state(); }

    void run_for(uint32_t for_ms) {
        const uint32_t until_ms = now_ms + for_ms;
        while (now_ms < until_ms) {
            now_ms += step_ms;
            simulator.step(now_ms);
        }
    }

    // Another client north of us at our height and speed, as a conforming encoder writes it.
    adsl::Packet traffic(uint32_t address, int32_t north_m, uint8_t version) {
        model::Ownship sender = state().own;
        sender.latitude_1e7 += north_m * 90;
        adsl::Packet packet{};
        REQUIRE(protocol::from_own(packet, sender, address, adsl::kAddressTableFlarm, 4));
        packet.set_protocol_version(version);
        return packet;
    }

    // A burst handed over as the chip delivers it, read by TrafficService on the next pass.
    void hear(adsl::Packet packet) {
        const uint8_t bytes = packet.data_bytes();
        packet.scramble(bytes);
        packet.set_crc(bytes);
        uint8_t chips[protocol::kTxChipBytes];
        const size_t chip_bytes =
            wire::manchester_frame(adsl::sync_word(bytes), packet.data, bytes, chips);
        events::RfEvent event{};
        event.type = events::RfEventType::RxDone;
        event.band = model::Band::M;
        event.rssi_dbm = -60;
        event.rssi_valid = true;
        event.bytes = models::Sx1262::deliver_after_sync(
            chips, static_cast<uint16_t>(chip_bytes), protocol::kSharedSync,
            protocol::kSharedSyncBits, event.data.data(), protocol::kRxChipBytes);
        REQUIRE(simulator.product().bus().rf.push(event));
        run_for(step_ms);
    }

    // The same burst on the channel at an absolute instant, heard only if the radio listens there.
    void emit(adsl::Packet packet, uint64_t at_us, uint32_t frequency_hz) {
        packet.scramble();
        packet.set_crc();
        uint8_t chips[protocol::kTxChipBytes];
        const size_t chip_bytes =
            wire::manchester_frame(adsl::kSyncWord, packet.data, adsl::kFrameBytes, chips);
        simulator.world().air().emit(at_us, frequency_hz, chips, static_cast<uint16_t>(chip_bytes),
                                     -60);
    }

    std::vector<OwnPosition> positions() {
        std::vector<OwnPosition> sent;
        for (const simulator::AirRecord& record :
             records_of(simulator.world().air(), adsl::Packet::kPayloadTypePosition)) {
            adsl::Packet packet{};
            REQUIRE(packet_of(record, packet));
            if (packet.address() != settings::address_of(platform::host::Identity::kSerial))
                continue;
            sent.push_back({record.at_us, packet.protocol_version(), packet.aircraft_category});
        }
        return sent;
    }
};

// Every position on air in Issue 2 as itself, or every one in Issue 1's form.
void check_written_in(const std::vector<OwnPosition>& sent, uint8_t version) {
    for (const OwnPosition& position : sent) {
        CHECK(int(position.version) == version);
        CHECK(int(position.category) == (version == 0 ? 3 : kHeavyRotorcraft));
    }
}

}  // namespace

// A client may send its Status before its first Traffic, and E.1.1 writes that Traffic in the
// lowest version that encodes it, so an Issue 2 client often sends version 0.
TEST_CASE(
    "ADS-L.4.SRD860.B.5: a Status heard before its sender's Traffic is kept and outranks it") {
    Flight flight;
    adsl::Packet status{};
    adsl::encode_status(status, kNeighbour, adsl::kAddressTableFlarm, adsl::StatusReport{});
    flight.hear(status);
    CHECK(flight.state().air.rx_status == 1);

    flight.simulator.world().air().clear();
    for (int second = 0; second < 8; second++) {
        flight.hear(flight.traffic(kNeighbour, kCloseNorthM, adsl::Packet::kProtocolVersionIssue1));
        flight.run_for(1000);
    }
    const std::vector<OwnPosition> sent = flight.positions();
    CHECK(sent.size() >= 6);
    check_written_in(sent, adsl::Packet::kProtocolVersionIssue2);
}

// The display keeps the nearest airborne targets, so a busy sky can leave a close client on the
// ground off it: B.5 counts the client all the same.
TEST_CASE("ADS-L.4.SRD860.B.5: a close Issue 1 client on the ground counts beside a full table") {
    Flight flight;
    for (int i = 0; i < kFarClients; i++)
        flight.hear(flight.traffic(kFarClientBase + static_cast<uint32_t>(i), kFarNorthM + i * 100,
                                   adsl::Packet::kProtocolVersionIssue2));
    REQUIRE(flight.state().traffic.count() == traffic::TrafficTable::kMaxTargets);

    adsl::Packet parked = flight.traffic(kNeighbour, kCloseNorthM, 0);
    parked.flight_state = static_cast<uint8_t>(adsl::FlightState::Ground);
    parked.set_altitude_m(flight.state().own.altitude_hae_mm / 1000 - 300);
    flight.hear(parked);
    CHECK(flight.state().traffic.find(adsl::kAddressTableFlarm, kNeighbour) < 0);

    flight.simulator.world().air().clear();
    flight.run_for(4000);
    const std::vector<OwnPosition> sent = flight.positions();
    CHECK(sent.size() >= 3);
    check_written_in(sent, adsl::Packet::kProtocolVersionIssue1);
}

// F.2's relay flag says another station sent it on the sender's behalf, so the sender may be out
// of our range (docs/ADSL_EASA_QUESTIONS.md, SB-Q10).
TEST_CASE("ADS-L.4.SRD860.B.5: a relayed Traffic leaves the version to the clients heard direct") {
    Flight flight;
    flight.simulator.world().air().clear();
    for (int second = 0; second < 6; second++) {
        adsl::Packet relayed = flight.traffic(kNeighbour, kCloseNorthM, 0);
        relayed.set_relay();
        flight.hear(relayed);
        flight.run_for(1000);
    }
    CHECK(flight.state().traffic.find(adsl::kAddressTableFlarm, kNeighbour) >= 0);
    const std::vector<OwnPosition> sent = flight.positions();
    CHECK(sent.size() >= 4);
    check_written_in(sent, adsl::Packet::kProtocolVersionIssue2);
}

// The same flight twice, a millisecond a step: alone, then with a close Issue 1 client keying on
// 868.4 MHz 25 ms before a position already armed there keys.
TEST_CASE("ADS-L.4.SRD860.B.5: a close Issue 1 client heard after a position is armed rewrites it" *
          doctest::test_suite("main")) {
    constexpr uint64_t kBeforeUs = 25000;
    OwnPosition planned{};
    {
        Flight alone(1);
        alone.run_for(6000);
        for (const OwnPosition& position : alone.positions()) {
            const int phase_ms = static_cast<int>(position.at_us / 1000 % 1000);
            if (phase_ms - timing::kWindowBMs.start_ms < 40) continue;
            planned = position;
            break;
        }
    }
    REQUIRE(planned.at_us != 0);
    CHECK(int(planned.version) == adsl::Packet::kProtocolVersionIssue2);

    Flight beside(1);
    const uint64_t heard_at_us = planned.at_us - kBeforeUs;
    beside.run_for(static_cast<uint32_t>(heard_at_us / 1000) - beside.now_ms - 1);
    REQUIRE(beside.state().rf.dwell.burst_armed);
    beside.emit(beside.traffic(kNeighbour, kCloseNorthM, 0), heard_at_us,
                timing::Scheduler::slot_frequency_hz(1));
    beside.run_for(static_cast<uint32_t>(kBeforeUs / 1000) + 20);

    CHECK(beside.state().traffic.find(adsl::kAddressTableFlarm, kNeighbour) >= 0);
    OwnPosition rewritten{};
    for (const OwnPosition& position : beside.positions())
        if (position.at_us / 1000 == planned.at_us / 1000) rewritten = position;
    REQUIRE(rewritten.at_us != 0);
    CHECK(rewritten.at_us == planned.at_us);
    CHECK(int(rewritten.version) == adsl::Packet::kProtocolVersionIssue1);
    CHECK(int(rewritten.category) == 3);
}

// D.3 lets a ground burst go irrespective of carrier detect, and a rewrite for B.5 keeps that.
TEST_CASE(
    "ADS-L.4.SRD860.B.5: a forced position rewritten for a close Issue 1 client stays forced") {
    constexpr uint8_t kModelPlane = 14;
    const int8_t held_dbm[1] = {-50};
    Flight parked;
    parked.simulator.product().settings().aircraft_type = kModelPlane;
    parked.simulator.world().set_speed_kt(0);
    parked.run_for(70000);
    parked.step_ms = 1;
    parked.simulator.platform().chips().radio.set_rssi_sequence(held_dbm, 1);
    const uint32_t until_ms = parked.now_ms + 30000;
    while (parked.state().air.tx_forced == 0 && parked.now_ms < until_ms) parked.run_for(1);
    REQUIRE(parked.state().air.tx_forced == 1);
    const std::optional<uint64_t> forced_at_us = parked.simulator.product().radio().forced_at_us();
    REQUIRE(forced_at_us);
    const uint32_t missed = parked.state().air.tx_busy_missed;

    parked.simulator.world().air().clear();
    parked.hear(parked.traffic(kNeighbour, kCloseNorthM, 0));
    parked.run_for(static_cast<uint32_t>(*forced_at_us / 1000) - parked.now_ms + 20);
    OwnPosition forced{};
    for (const OwnPosition& position : parked.positions())
        if (position.at_us == *forced_at_us) forced = position;
    REQUIRE(forced.at_us != 0);
    CHECK(int(forced.version) == adsl::Packet::kProtocolVersionIssue1);
    CHECK(parked.state().air.tx_busy_missed == missed);
}
