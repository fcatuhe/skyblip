// ADS-L 4 SRD-860 issue 2 Subpart B, the overview: bit order, transmission sequence and versioning.
#include <cstdint>
#include <cstring>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/radio/log.h"
#include "core/settings/address.h"
#include "core/traffic/table.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "protocols/adsl/packet.h"
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

// What RadioService decides: a position goes out only in a version every close client reads.
bool sent_beside(const traffic::TrafficTable& table, const model::Ownship& own, uint8_t category,
                 adsl::Packet& packet) {
    REQUIRE(
        protocol::from_own(packet, own, 0x5AFE01, settings::kSkyblipAddressMappingTable, category));
    return packet.protocol_version() <= table.readable_version(own, own.fix_at_ms) ||
           protocol::issue1_form(packet, own);
}

}  // namespace

// Beside an Issue 1 client category 15 goes as Issue 1's rotorcraft, and alone as itself.
TEST_CASE("ADS-L.4.SRD860.B.5: the version transmitted is one every close neighbour can read") {
    const model::Ownship own = flying_own();
    traffic::TrafficTable table;
    table.set_own_reference(own);
    REQUIRE(table.update(neighbour(own, 500, 0), own.utc_s) >= 0);
    adsl::Packet packet{};
    REQUIRE(sent_beside(table, own, kHeavyRotorcraft, packet));
    CHECK(packet.protocol_version() == adsl::Packet::kProtocolVersionIssue1);
    CHECK(int(packet.aircraft_category) == 3);
    REQUIRE(sent_beside(table, own, 4, packet));
    CHECK(packet.protocol_version() == adsl::Packet::kProtocolVersionIssue1);

    traffic::TrafficTable empty;
    REQUIRE(sent_beside(empty, own, kHeavyRotorcraft, packet));
    CHECK(packet.protocol_version() == adsl::Packet::kProtocolVersionIssue2);
    CHECK(int(packet.aircraft_category) == kHeavyRotorcraft);
}

// Issue 1 has no code for an unknown altitude, speed or climb, and we never claim one we lack.
TEST_CASE("ADS-L.4.SRD860.B.5: beside an Issue 1 client a position with an unknown field is held") {
    const model::Ownship own = flying_own();
    traffic::TrafficTable table;
    table.set_own_reference(own);
    REQUIRE(table.update(neighbour(own, 500, 0), own.utc_s) >= 0);
    adsl::Packet packet{};

    model::Ownship two_d = own;
    two_d.vdop_e2 = 0;
    CHECK_FALSE(sent_beside(table, two_d, 4, packet));
    model::Ownship no_climb = own;
    no_climb.climb_valid = false;
    CHECK_FALSE(sent_beside(table, no_climb, kHeavyRotorcraft, packet));

    traffic::TrafficTable empty;
    REQUIRE(sent_beside(empty, two_d, 4, packet));
    CHECK_FALSE(packet.has_altitude());
}

TEST_CASE("ADS-L.4.SRD860.B.5: a neighbour that is neither close nor closing leaves the version") {
    model::Ownship own = flying_own();
    own.track_cdeg = 27000;
    traffic::TrafficTable table;
    table.set_own_reference(own);
    REQUIRE(table.update(neighbour(own, 5000, 0), own.utc_s) >= 0);
    CHECK(table.readable_version(own, own.fix_at_ms) == adsl::Packet::kMaxProtocolVersion);

    model::AircraftObservation above = neighbour(own, 500, 0);
    above.altitude_m = 1900;
    traffic::TrafficTable stacked;
    stacked.set_own_reference(own);
    REQUIRE(stacked.update(above, own.utc_s) >= 0);
    CHECK(stacked.readable_version(own, own.fix_at_ms) == adsl::Packet::kMaxProtocolVersion);
}

// 4 km ahead, closing at 150 m/s head-on: 27 s to CPA, inside B.5's 30.
TEST_CASE("ADS-L.4.SRD860.B.5: a neighbour less than 30 s from CPA counts as close") {
    model::Ownship own = flying_own();
    own.speed_mm_s = 75000;
    traffic::TrafficTable table;
    table.set_own_reference(own);
    model::AircraftObservation head_on = neighbour(own, 4000, 0);
    head_on.speed_q = 300;
    head_on.track_c9 = 384;
    REQUIRE(table.update(head_on, own.utc_s) >= 0);
    CHECK(table.readable_version(own, own.fix_at_ms) == adsl::Packet::kProtocolVersionIssue1);
}

// B.5 reads a Traffic packet's version only for a client whose Status has not been heard.
TEST_CASE("ADS-L.4.SRD860.B.5: a neighbour's Status outranks the version its Traffic came in") {
    const model::Ownship own = flying_own();
    traffic::TrafficTable table;
    table.set_own_reference(own);
    REQUIRE(table.update(neighbour(own, 500, 0), own.utc_s) >= 0);
    REQUIRE(table.readable_version(own, own.fix_at_ms) == 0);

    CHECK(table.declare_max_version(adsl::kAddressTableFlarm, kNeighbour, 1));
    CHECK(table.readable_version(own, own.fix_at_ms) == 1);
    REQUIRE(table.update(neighbour(own, 450, 0), own.utc_s + 1) >= 0);
    CHECK(table.readable_version(own, own.fix_at_ms) == 1);
    CHECK_FALSE(table.declare_max_version(adsl::kAddressTableFlarm, 0x111111, 0));
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
    const int index = rig.state().traffic.find(adsl::kAddressTableFlarm, kNeighbour);
    REQUIRE(index >= 0);
    CHECK(int(rig.state().traffic.at(index)->version.max) == 0);

    adsl::Packet status{};
    adsl::encode_status(status, kNeighbour, adsl::kAddressTableFlarm, adsl::StatusReport{});
    hear_adsl(rig, status);
    settle(rig, now_ms);

    CHECK(heard_verdict(rig) == radio::Verdict::Declared);
    CHECK(rig.state().air.rx_status == 1);
    CHECK(rig.state().traffic.at(index)->version.declared);
    CHECK(int(rig.state().traffic.at(index)->version.max) == adsl::Packet::kMaxProtocolVersion);
}

TEST_CASE("ADS-L.4.SRD860.B.5: a Status from an aircraft not in the table changes nothing") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    fly(rig, now_ms, 3);

    adsl::Packet status{};
    adsl::encode_status(status, kNeighbour, adsl::kAddressTableFlarm, adsl::StatusReport{});
    hear_adsl(rig, status);
    settle(rig, now_ms);

    CHECK(heard_verdict(rig) == radio::Verdict::Unsupported);
    CHECK(rig.state().air.rx_status == 0);
    CHECK(rig.state().traffic.count() == 0);
}

namespace {

struct Flown {
    int sent{0};
    uint32_t held{0};
};

// Every simulated aircraft sends version 0, so to a Go it is an Issue 1 client until a Status says
// more.
Flown flown_beside_issue1(uint8_t category, uint8_t sent_as) {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.product().settings().aircraft_type = category;
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(60);
    simulator.world().add_aircraft(0, 400, 0, 30, 90);
    const uint32_t settled_ms = past_settling(simulator);
    const uint32_t held_before = simulator.product().state().air.tx_held_for_b5;
    run_on(simulator, settled_ms, 15000);

    Flown flown;
    for (const simulator::AirRecord& record :
         records_of(simulator.world().air(), adsl::Packet::kPayloadTypePosition)) {
        adsl::Packet packet{};
        REQUIRE(packet_of(record, packet));
        if (packet.address() != settings::address_of(platform::host::Identity::kSerial)) continue;
        CHECK(packet.protocol_version() == adsl::Packet::kProtocolVersionIssue1);
        CHECK(packet.aircraft_category == sent_as);
        flown.sent++;
    }
    flown.held = simulator.product().state().air.tx_held_for_b5 - held_before;
    return flown;
}

}  // namespace

TEST_CASE(
    "ADS-L.4.SRD860.B.5: beside an Issue 1 client a heavy rotorcraft goes out as rotorcraft") {
    const Flown heavy = flown_beside_issue1(kHeavyRotorcraft, 3);
    CHECK(heavy.sent > 10);
    CHECK(heavy.held == 0);
}

TEST_CASE("ADS-L.4.SRD860.B.5: beside an Issue 1 client a glider still goes out every second") {
    const Flown glider = flown_beside_issue1(4, 4);
    CHECK(glider.sent > 10);
    CHECK(glider.held == 0);
}
