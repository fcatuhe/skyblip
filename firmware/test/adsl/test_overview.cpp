// ADS-L 4 SRD-860 issue 2 Subpart B, the overview: bit order, transmission sequence and versioning.
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/bus/state.h"
#include "core/events/rf.h"
#include "core/model/aircraft.h"
#include "core/model/band.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/air.h"
#include "core/radio/log.h"
#include "core/settings/address.h"
#include "core/timing/slot.h"
#include "core/traffic/table.h"
#include "core/traffic/versions.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/platform/host/identity.h"
#include "protocols/adsl/flight_state.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/physical.h"
#include "protocols/common/manchester.h"
#include "simulator/simulator.h"
#include "test/support/m_band_rig.h"
#include "test/support/product_rig.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

constexpr uint32_t kNeighbour = 0xC5D804;
constexpr uint8_t kHeavyRotorcraft = 15;

model::Ownship flying_own() {
    model::Ownship own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.utc_s = 1785628800;
    own.fix_at_ms = 10000;
    own.latitude_1e7 = 485000000;
    own.longitude_1e7 = 85000000;
    own.altitude_hae_mm = 1000000;
    own.speed_mm_s = 30000;
    own.track_cdeg = 9000;
    own.hdop_e2 = 90;
    own.vdop_e2 = 120;
    own.climb_valid = true;
    own.flight_state = 2;
    return own;
}

model::AircraftObservation neighbour(const model::Ownship& own, int32_t east_m, uint8_t version) {
    model::AircraftObservation observation{};
    observation.address = kNeighbour;
    observation.address_mapping_table = adsl::kAddressTableFlarm;
    observation.aircraft_category = 4;
    observation.flight_state = 2;
    observation.latitude_1e7 = own.latitude_1e7;
    observation.longitude_1e7 =
        own.longitude_1e7 + static_cast<int32_t>(static_cast<int64_t>(east_m) * 1000000 / 7460);
    observation.altitude_m = 1000;
    observation.speed_q = 120;
    observation.speed_valid = true;
    observation.track_c9 = 128;
    observation.position_valid = true;
    observation.source = model::Source::AdslDirect;
    observation.received.at_s = own.utc_s;
    observation.at_ms = own.fix_at_ms;
    observation.adsl_version = version;
    return observation;
}

}  // namespace

TEST_CASE("ADS-L.4.SRD860.B.5: a neighbour that is neither close nor closing leaves the version") {
    model::Ownship own = flying_own();
    own.track_cdeg = 27000;
    traffic::VersionTable versions;
    versions.hear_traffic(neighbour(own, 5000, 0), own.utc_s);
    CHECK(versions.readable_version(own, own.fix_at_ms) == adsl::Packet::kMaxProtocolVersion);

    model::AircraftObservation above = neighbour(own, 500, 0);
    above.altitude_m = 1900;
    traffic::VersionTable stacked;
    stacked.hear_traffic(above, own.utc_s);
    CHECK(stacked.readable_version(own, own.fix_at_ms) == adsl::Packet::kMaxProtocolVersion);
}

// 4 km ahead, closing at 150 m/s head-on: 27 s to CPA, inside B.5's 30.
TEST_CASE("ADS-L.4.SRD860.B.5: a neighbour less than 30 s from CPA counts as close") {
    model::Ownship own = flying_own();
    own.speed_mm_s = 75000;
    traffic::VersionTable versions;
    model::AircraftObservation head_on = neighbour(own, 4000, 0);
    head_on.speed_q = 300;
    head_on.track_c9 = 384;
    versions.hear_traffic(head_on, own.utc_s);
    CHECK(versions.readable_version(own, own.fix_at_ms) == adsl::Packet::kProtocolVersionIssue1);
}

// B.5 reads a Traffic packet's version only for a client whose Status has not been heard.
TEST_CASE("ADS-L.4.SRD860.B.5: a neighbour's Status outranks the version its Traffic came in") {
    const model::Ownship own = flying_own();
    traffic::VersionTable versions;
    versions.hear_traffic(neighbour(own, 500, 0), own.utc_s);
    REQUIRE(versions.readable_version(own, own.fix_at_ms) == 0);

    versions.hear_status(adsl::kAddressTableFlarm, kNeighbour, 1, own.utc_s);
    CHECK(versions.readable_version(own, own.fix_at_ms) == 1);
    versions.hear_traffic(neighbour(own, 450, 0), own.utc_s + 1);
    CHECK(versions.readable_version(own, own.fix_at_ms) == 1);

    // A Status with no Traffic behind it has no position, so it can be close to nobody yet.
    versions.hear_status(adsl::kAddressTableFlarm, 0x111111, 0, own.utc_s + 1);
    CHECK(versions.count() == 2);
    CHECK(versions.readable_version(own, own.fix_at_ms) == 1);
}

TEST_CASE("ADS-L.4.SRD860.B.5: a Status heard from a tracked aircraft is filed against it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    fly(rig, now_ms, 3);

    model::Ownship transmitter = rig.state().own;
    transmitter.latitude_1e7 += 10000;
    adsl::Packet traffic{};
    REQUIRE(protocol::from_own(traffic, transmitter, kNeighbour, adsl::kAddressTableFlarm, 4));
    traffic.set_protocol_version(0);
    hear_adsl(rig, traffic);
    settle(rig, now_ms);
    const traffic::VersionTable& versions = rig.product.versions();
    const int index = versions.find(adsl::kAddressTableFlarm, kNeighbour);
    REQUIRE(index >= 0);
    CHECK(int(versions.at(index)->max_version()) == 0);

    adsl::Packet status{};
    adsl::encode_status(status, kNeighbour, adsl::kAddressTableFlarm, adsl::StatusReport{});
    hear_adsl(rig, status);
    settle(rig, now_ms);

    CHECK(heard_verdict(rig) == radio::Verdict::Declared);
    CHECK(rig.state().air.rx_status == 1);
    CHECK(versions.at(index)->declared);
    CHECK(int(versions.at(index)->max_version()) == adsl::Packet::kMaxProtocolVersion);
}

TEST_CASE("ADS-L.4.SRD860.B.5: a Status from an aircraft not on the display is kept for it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    fly(rig, now_ms, 3);

    adsl::Packet status{};
    adsl::encode_status(status, kNeighbour, adsl::kAddressTableFlarm, adsl::StatusReport{});
    hear_adsl(rig, status);
    settle(rig, now_ms);

    CHECK(heard_verdict(rig) == radio::Verdict::Declared);
    CHECK(rig.state().air.rx_status == 1);
    CHECK(rig.state().traffic.count() == 0);
    const traffic::VersionTable& versions = rig.product.versions();
    const int index = versions.find(adsl::kAddressTableFlarm, kNeighbour);
    REQUIRE(index >= 0);
    CHECK(versions.at(index)->declared);
    CHECK_FALSE(versions.at(index)->placed);
    CHECK(int(versions.at(index)->max_version()) == adsl::Packet::kMaxProtocolVersion);
}

// B.5 is kept for the clients heard direct, and F.2's relay flag says another station sent this.
TEST_CASE("ADS-L.4.SRD860.B.5: a relayed Status is not filed") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    fly(rig, now_ms, 3);

    adsl::Packet status{};
    adsl::encode_status(status, kNeighbour, adsl::kAddressTableFlarm, adsl::StatusReport{});
    status.set_relay();
    hear_adsl(rig, status);
    settle(rig, now_ms);

    CHECK(heard_verdict(rig) == radio::Verdict::Unsupported);
    CHECK(rig.state().air.rx_status == 0);
    CHECK(rig.product.versions().count() == 0);
}

namespace {

struct Flown {
    int sent{0};
    uint32_t held{0};
    uint32_t given_up{0};
    uint8_t version{0};
    uint8_t category{0};
};

// Every simulated aircraft sends version 0, so to a Go it is an Issue 1 client until a Status says
// more. A VDOP of 0 is a 2D fix, whose altitude goes out as unknown.
Flown flown(uint8_t category, bool beside_issue1, uint16_t vdop_e2 = 150, int speed_kt = 60,
            uint32_t for_ms = 15000) {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.product().settings().aircraft_type = category;
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(speed_kt);
    simulator.world().gnss().vdop_e2 = vdop_e2;
    if (beside_issue1) simulator.world().add_aircraft(0, 400, 0, speed_kt == 0 ? 1 : 30, 90);
    const uint32_t settled_ms = past_settling(simulator);
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

    const Flown alone = flown(4, false, /*vdop_e2=*/0);
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

// F.2's relay flag says another station sent it on the sender's behalf: the sender may be out of
// our range, and the version may be the relay's.
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
