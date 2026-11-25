// ADS-L 4 SRD-860 issue 2 Subpart B, the overview: bit order, transmission sequence and versioning.
#include <cstdint>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/radio/log.h"
#include "core/settings/address.h"
#include "core/traffic/table.h"
#include "core/traffic/versions.h"
#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"
#include "test/support/armings_rig.h"
#include "test/support/m_band_rig.h"
#include "test/support/product_rig.h"
#include "test/support/traffic_scene.h"

using namespace skyblip;

namespace {

constexpr uint32_t kNeighbour = 0xC5D804;
constexpr uint32_t kAircraftAddress = 0x3C6586;

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
    own.speed_valid = true;
    own.track_valid = true;
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

// A 2D fix knows no height, so an Issue 1 client 900 m above may be level with us, and is close.
TEST_CASE("ADS-L.4.SRD860.B.5: with no height of its own, a Go counts a client overhead as close") {
    model::Ownship own = flying_own();
    own.altitude_hae_valid = false;
    model::AircraftObservation above = neighbour(own, 500, 0);
    above.altitude_m = 1900;
    traffic::VersionTable versions;
    versions.hear_traffic(above, own.utc_s);
    CHECK(versions.readable_version(own, own.fix_at_ms) == adsl::Packet::kProtocolVersionIssue1);
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

// Own speed unknown, the closure is too: 30 s at G.1.8's fastest 236 m/s is 7080 m, plus its own.
TEST_CASE("ADS-L.4.SRD860.B.5: with no own speed, a client 30 s away at 236 m/s counts as close") {
    model::Ownship own = flying_own();
    own.speed_mm_s = 0;
    own.speed_valid = false;
    const auto readable = [&](const model::Ownship& from, int32_t east_m) {
        traffic::VersionTable versions;
        model::AircraftObservation hovering = neighbour(from, east_m, 0);
        hovering.speed_q = 0;
        versions.hear_traffic(hovering, from.utc_s);
        return versions.readable_version(from, from.fix_at_ms);
    };
    CHECK(readable(own, 2500) == adsl::Packet::kProtocolVersionIssue1);
    CHECK(readable(own, 7000) == adsl::Packet::kProtocolVersionIssue1);
    CHECK(readable(own, 7200) == adsl::Packet::kMaxProtocolVersion);

    model::Ownship trackless = flying_own();
    trackless.track_cdeg = 27000;
    trackless.track_valid = false;
    CHECK(readable(trackless, 2500) == adsl::Packet::kProtocolVersionIssue1);
    CHECK(readable(flying_own(), 2500) == adsl::Packet::kMaxProtocolVersion);
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

// The display refuses the addresses we transmit under, and an echo of our own Traffic is no client.
TEST_CASE("ADS-L.4.SRD860.B.5: our own Traffic and Status heard back set no version") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    fly(rig, now_ms, 3);
    const settings::AirAddress serial_address = rig.state().activation.address();
    REQUIRE(serial_address);
    model::Ownship echo = rig.state().own;
    echo.latitude_1e7 += 1000;

    adsl::Packet traffic{};
    REQUIRE(protocol::from_own(traffic, echo, serial_address, settings::kSkyblipAddressMappingTable,
                               4));
    traffic.set_protocol_version(0);
    hear_adsl(rig, traffic);
    settle(rig, now_ms);
    adsl::Packet status{};
    REQUIRE(protocol::from_own_status(status, serial_address, settings::kSkyblipAddressMappingTable,
                                      adsl::StatusReport{}));
    hear_adsl(rig, status);
    settle(rig, now_ms);
    CHECK(rig.state().air.rx_status == 0);

    rig.settings().aircraft_address = kAircraftAddress;
    settle(rig, now_ms);
    adsl::Packet under_icao{};
    REQUIRE(protocol::from_own(under_icao, echo, settings::AirAddress{kAircraftAddress},
                               adsl::kAddressTableIcao, 4));
    under_icao.set_protocol_version(0);
    hear_adsl(rig, under_icao);
    settle(rig, now_ms);
    CHECK(rig.product.versions().count() == 0);
}

// The executor reads the channel from 2.2 ms before the instant and owns the burst from then.
TEST_CASE("ADS-L.4.SRD860.B.5: a client heard once our burst's channel check has begun waits") {
    Armings armings;
    armings.ready_to_transmit(1000);
    model::Ownship& own = armings.state.own;
    own.latitude_1e7 = 481000000;
    own.longitude_1e7 = 81000000;
    own.altitude_hae_mm = 1000000;
    own.hdop_e2 = 90;
    own.vdop_e2 = 120;
    own.climb_valid = true;
    own.aircraft_category = 15;
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.tick_in(0, 420);
    REQUIRE(armings.rf.with_tx == 1);

    armings.rf.listening = true;
    model::AircraftObservation client = target_at(own, 300, 0, 0, 30, 90, own.fix_at_ms);
    client.adsl_version = adsl::Packet::kProtocolVersionIssue1;
    client.source = model::Source::AdslDirect;
    client.received.at_s = 1000;
    armings.versions.hear_traffic(client, 1000);
    armings.tick_in(0, 430);
    CHECK(armings.rf.withdrawn == 0);
    CHECK(armings.rf.with_tx == 1);
}
