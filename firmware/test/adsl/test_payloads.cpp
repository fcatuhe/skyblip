// ADS-L 4 SRD-860 issue 2 G.2 to G.5: the Status, Traffic Uplink, FIS-B and Remote ID payloads.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/comms/peer.h"
#include "core/flight/state.h"
#include "core/gnss/first_fix.h"
#include "core/model/aircraft.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/settings/address.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "products/skyblip_go/settings.h"
#include "protocols/adsb/squitter.h"
#include "protocols/adsl/test/conformance/reference.h"
#include "protocols/adsl/uplink.h"
#include "protocols/common/bit_field.h"
#include "simulator/paired_simulator.h"
#include "simulator/simulator.h"
#include "simulator/world/adsb_out.h"
#include "test/support/product_rig.h"
#include "test/support/simulator_run.h"
#include "test/support/tango.h"

using namespace skyblip;

namespace {
constexpr uint32_t kUplinkUtcS = 1785628800;
constexpr uint32_t kStation = 0x00FFFE;

model::AircraftObservation target(uint32_t address) {
    model::AircraftObservation observation{};
    observation.address = address;
    observation.address_mapping_table = 6;
    observation.aircraft_category = 1;
    observation.flight_state = 2;
    observation.latitude_1e7 = 481234500;
    observation.longitude_1e7 = 81234500;
    observation.altitude_m = 1200;
    observation.speed_q = 160;
    observation.speed_valid = true;
    observation.position_valid = true;
    observation.altitude_valid = true;
    observation.emergency = adsl::kEmergencyNone;
    return observation;
}
const adsl::Packet& status_heard() {
    static const adsl::Packet packet = [] {
        const std::vector<simulator::AirRecord> statuses = statuses_flown(50, 21);
        REQUIRE_FALSE(statuses.empty());
        adsl::Packet heard{};
        REQUIRE(packet_of(statuses.front(), heard));
        return heard;
    }();
    return packet;
}

// A Go and its Bravo in one aircraft, the link left up or dropped, and the Go's next Status.
adsl::Packet status_beside_bravo(bool link_up, comms::BravoState& bravo) {
    simulator::PairedSimulator paired;
    REQUIRE(paired.setup() == Status::Ok);
    paired.world().set_fix(true);
    paired.world().set_speed_kt(50);
    paired.aboard().set_fix(true);
    paired.aboard().set_link(link_up);
    paired.run(gnss::kFirstFixSettleMs);
    paired.world().air().clear();
    const uint32_t until_ms =
        gnss::kFirstFixSettleMs + (timing::Transmitter::kStatusPeriodS + 1) * 1000;
    for (uint32_t now_ms = gnss::kFirstFixSettleMs; now_ms <= until_ms;
         now_ms += simulator::PairedSimulator::kStepMs)
        paired.step(now_ms);
    bravo = paired.product().state().bravo;
    const std::vector<simulator::AirRecord> statuses =
        records_of(paired.world().air(), adsl::Packet::kPayloadTypeStatus);
    REQUIRE_FALSE(statuses.empty());
    adsl::Packet heard{};
    REQUIRE(packet_of(statuses.back(), heard));
    return heard;
}

uint32_t status_field(wire::BitField where) { return status_heard().field(where); }

// INFO: fc 19nov26 far louder than the world's squitters, as a transponder a metre from Bravo is
constexpr int8_t kCockpitDbm = -10;

adsl::Packet status_beside_cockpit(const simulator::AdsbAir& air) {
    simulator::PairedSimulator paired;
    REQUIRE(paired.setup() == Status::Ok);
    paired.world().set_fix(true);
    paired.world().set_speed_kt(0);
    paired.aboard().set_fix(true);
    paired.aboard().set_link(true);
    paired.run(gnss::kFirstFixSettleMs);
    const uint32_t until_ms =
        gnss::kFirstFixSettleMs + (2 * timing::Transmitter::kStatusPeriodS + 1) * 1000;
    for (uint32_t now_ms = gnss::kFirstFixSettleMs; now_ms <= until_ms;
         now_ms += simulator::PairedSimulator::kStepMs) {
        if (now_ms % 500 == 0)
            paired.bravo_platform().radio().receive_air(air.chips, sizeof(air.chips), kCockpitDbm);
        paired.step(now_ms);
    }
    REQUIRE(comms::address_of(paired.product().state().own_transponder) ==
            simulator::PairedSimulator::kAircraftAddress);
    const std::vector<simulator::AirRecord> statuses =
        records_of(paired.world().air(), adsl::Packet::kPayloadTypeStatus);
    REQUIRE_FALSE(statuses.empty());
    adsl::Packet heard{};
    REQUIRE(packet_of(statuses.back(), heard));
    return heard;
}

simulator::AdsbAir cockpit_identification() {
    return simulator::adsb_air(
        adsb::identification(simulator::PairedSimulator::kAircraftAddress, 5, 4, 1, "FHBXA"));
}

simulator::AdsbAir cockpit_all_call() {
    return simulator::mode_s_air(adsb::all_call(simulator::PairedSimulator::kAircraftAddress, 5));
}

constexpr uint32_t kOwnTransponder = 0x3A4B5C;

void confirm_on_ground(Tango& tango, uint32_t aircraft_address, uint32_t heard,
                       const std::string& more) {
    tango.bravo.health.aircraft_address_set = true;
    tango.pair_and_store();
    tango.go.settings().aircraft_address = aircraft_address;
    tango.go.seconds(tango.now_ms, 5, 0, 500);
    REQUIRE(tango.go.state().flight.confirmed_state == flight::FlightState::Ground);
    tango.bravo.health.own_transponder_fields =
        "\"own_transponder\":" + std::to_string(heard) + more;
    tango.run_for(2500);
    REQUIRE(comms::address_of(tango.go.state().own_transponder) == heard);
}

adsl::Packet status_beside_transponder(uint32_t aircraft_address, uint32_t heard,
                                       const std::string& more) {
    Tango tango;
    confirm_on_ground(tango, aircraft_address, heard, more);
    adsl::Packet packet{};
    adsl::encode_status(packet, settings::address_of(platform::host::Identity::kSerial),
                        settings::kSkyblipAddressMappingTable,
                        tango.go.product.radio().status_report());
    return packet;
}

// An aircraft 2 m above own-ship, north of it, as a ground station relays its Traffic this second.
adsl::Packet relayed_near(Rig& rig, uint32_t address, int32_t north_m) {
    const model::Ownship& own = rig.state().own;
    adsl::Traffic traffic = adsl::conformance::reference_traffic();
    traffic.address = address;
    traffic.timestamp = adsl::timestamp_code(own.utc_s + 1, timing::kGroundEmitStartMs);
    traffic.latitude_1e7 = own.latitude_1e7 + north_m * 90;
    traffic.longitude_1e7 = own.longitude_1e7;
    traffic.altitude_m = own.altitude_hae_mm / 1000 + 2;
    adsl::Packet packet{};
    adsl::encode_traffic(packet, adsl::kAddressTableFlarm, traffic);
    return packet;
}

// C.4.3's sync word, then the packet, into the O-band dwell the Go armed for the Uplink slot.
bool hear_uplink(Rig& rig, const uint8_t* packet, int bytes) {
    uint8_t burst[adsl::kUplinkMaxBurstBytes] = {0x2D, 0xD4};
    std::memcpy(burst + 2, packet, static_cast<size_t>(bytes));
    rig.push_timed_fix(25000, 900);
    const uint32_t at_ms = rig.now_ms + timing::kGroundEmitStartMs;
    rig.run(rig.now_ms, rig.now_ms + 200);
    rig.platform.clock().set_milliseconds(at_ms);
    rig.product.run_pass(at_ms);
    const bool heard = rig.platform.chips().radio.receive_air(
        burst, static_cast<uint16_t>(2 + bytes), false, -92, adsl::kUplinkChipRateBps);
    rig.run(at_ms + 10, rig.now_ms + 950);
    rig.now_ms += 1000;
    rig.utc_offset_s++;
    return heard;
}

}  // namespace

TEST_CASE(
    "ADS-L.4.SRD860.G.2: a Status payload declares this device's bands, systems and version") {
    const adsl::Packet& packet = status_heard();
    CHECK(packet.payload_type == 3);
    CHECK(packet.address() == settings::address_of(platform::host::Identity::kSerial));
    // E.1.1: the Status payload is Issue 2's, so its network header says version 1.
    CHECK((packet.version & 0x0F) == 1);
    // 24 reserved bits close the 56 the payload table lists.
    CHECK(wire::read_bits(packet.payload, wire::BitField{72, 24}) == 0);
}

TEST_CASE("ADS-L.4.SRD860.G.2.1: the Status payload names the highest version this device reads") {
    CHECK(status_field(wire::BitField{40, 4}) == 1);
}

// The dwells hear 868.2 MHz from 450 to 822 ms and 868.4 MHz from 827 to 1000, of the direct slot's
// 550.
TEST_CASE("ADS-L.4.SRD860.G.2.2: the Status payload declares the M-band dwell as a receive class") {
    CHECK(timing::Scheduler::direct_heard_permille() == (372 + 173) * 1000 / 1100);
    CHECK(status_field(wire::BitField{44, 2}) == 2);
}

TEST_CASE("ADS-L.4.SRD860.G.2.3: the Status payload declares the O-band LDR receive class") {
    CHECK(status_field(wire::BitField{46, 2}) == 0);
}

// The O-band dwell sits in the uplink slot, which the clause says does not count.
TEST_CASE("ADS-L.4.SRD860.G.2.4: the Status payload declares the O-band HDR receive class") {
    CHECK(timing::dwell_of(timing::kUplinkWindowMs).end_ms <= adsl::kDirectStartMs);
    CHECK(status_field(wire::BitField{48, 2}) == 0);
}

TEST_CASE("ADS-L.4.SRD860.G.2.5: the Status payload declares the ADS-L 4 Mobile state") {
    CHECK(status_field(wire::BitField{50, 2}) == 0);
}

// Bit 0 is the ALP-TAS wire this device decodes, bit 4 the ADS-B a paired Bravo hears.
TEST_CASE("ADS-L.4.SRD860.G.2.6: the Status payload declares which other systems are received") {
    CHECK(status_field(wire::BitField{52, 8}) == 0x01);

    comms::BravoState bravo = comms::BravoState::Unpaired;
    CHECK(status_beside_bravo(true, bravo).field(wire::BitField{52, 8}) == 0x11);
    CHECK(bravo == comms::BravoState::Live);
}

// G.2.6 leaves an unknown capability unset, and a Bravo the Go cannot reach hears nothing for it.
TEST_CASE("ADS-L.4.SRD860.G.2.6: a paired Bravo the Go cannot reach is not declared") {
    comms::BravoState bravo = comms::BravoState::Unpaired;
    CHECK(status_beside_bravo(false, bravo).field(wire::BitField{52, 8}) == 0x01);
    CHECK(bravo != comms::BravoState::Live);
    CHECK(bravo != comms::BravoState::Unpaired);
}

// A Go alone knows of no other system aboard, and G.2.7 leaves an unknown one unset.
TEST_CASE("ADS-L.4.SRD860.G.2.7: a Go with no Bravo declares no other system transmitted") {
    CHECK(status_field(wire::BitField{60, 8}) == 0);
}

// An identification Bravo heard from the entered address is an extended squitter, so ADS-B out.
TEST_CASE("ADS-L.4.SRD860.G.2.7: ADS-B 1090 is declared once the entered address identifies") {
    const adsl::Packet named = status_beside_transponder(kOwnTransponder, kOwnTransponder,
                                                         ",\"own_transponder_callsign\":\"FHBXA\"");
    CHECK(named.field(wire::BitField{60, 8}) == adsl::conspicuity::kAdsb1090);

    const adsl::Packet silent = status_beside_transponder(kOwnTransponder, kOwnTransponder, "");
    CHECK(silent.field(wire::BitField{60, 8}) == 0);
}

// The cockpit transponder at the entered address, heard by Bravo on the ground, on the Go's Status.
TEST_CASE("ADS-L.4.SRD860.G.2.7: the Status on air carries the ADS-B out Bravo heard") {
    CHECK(status_beside_cockpit(cockpit_identification()).field(wire::BitField{60, 8}) ==
          adsl::conspicuity::kAdsb1090);
    CHECK(status_beside_cockpit(cockpit_all_call()).field(wire::BitField{60, 8}) == 0);
}

TEST_CASE("ADS-L.4.SRD860.G.2.8: the Status on air carries the Mode S transponder Bravo heard") {
    CHECK(status_beside_cockpit(cockpit_all_call()).field(wire::BitField{68, 2}) == 2);
}

TEST_CASE("ADS-L.4.SRD860.G.2.8: a Go with no Bravo declares no transponder, which is unknown") {
    CHECK(status_field(wire::BitField{68, 2}) == 0);
}

// The pilot's entry of the address Bravo hears is what makes it ours, not a neighbour's.
TEST_CASE("ADS-L.4.SRD860.G.2.8: Mode S is declared once Bravo hears the entered address") {
    CHECK(status_beside_transponder(kOwnTransponder, kOwnTransponder, "")
              .field(wire::BitField{68, 2}) == 2);
    CHECK(status_beside_transponder(go::kAircraftAddressUnset, kOwnTransponder, "")
              .field(wire::BitField{68, 2}) == 0);
    CHECK(status_beside_transponder(0x3A0001, kOwnTransponder, "").field(wire::BitField{68, 2}) ==
          0);
}

// Bravo names the aircraft's transponder on the ground only, and the aircraft keeps it in flight.
TEST_CASE("ADS-L.4.SRD860.G.2.8: a transponder confirmed on the ground stays declared in flight" *
          doctest::test_suite("main")) {
    Tango tango;
    confirm_on_ground(tango, kOwnTransponder, kOwnTransponder,
                      ",\"own_transponder_callsign\":\"FHBXA\"");
    tango.go.seconds(tango.now_ms, 20, 25000, 900);
    REQUIRE(tango.go.state().flight.confirmed_state == flight::FlightState::Airborne);
    REQUIRE_FALSE(tango.go.state().own_transponder);
    adsl::StatusReport report = tango.go.product.radio().status_report();
    CHECK(report.transponder == adsl::Transponder::ModeS);
    CHECK(report.conspicuity_transmitted == adsl::conspicuity::kAdsb1090);

    tango.go.settings().aircraft_address = 0x3A0001;
    report = tango.go.product.radio().status_report();
    CHECK(report.transponder == adsl::Transponder::None);
    CHECK(report.conspicuity_transmitted == 0);

    tango.go.seconds(tango.now_ms, 2, 25000, 900);
    report = tango.go.product.radio().status_report();
    CHECK(report.transponder == adsl::Transponder::None);
    CHECK(report.conspicuity_transmitted == 0);
}

TEST_CASE("ADS-L.4.SRD860.G.2.9: the Status payload declares that Traffic Uplink is processed") {
    CHECK(status_field(wire::BitField{70, 1}) == 1);
}

TEST_CASE("ADS-L.4.SRD860.G.2.10: the Status payload declares whether FIS-B is processed") {
    CHECK(status_field(wire::BitField{71, 1}) == 0);
}

// Twenty seconds from second to second, and the instant inside each one is the direct slot's own
// draw.
// Gaps alternate 11 and 9 s, so a receiver parked on one channel still hears one every 20 s.
TEST_CASE("ADS-L.4.SRD860.G.2.11: a Status payload goes out at least every 20 seconds" *
          doctest::test_suite("main")) {
    constexpr uint64_t kSlotUs = (adsl::kDirectEndMs - adsl::kDirectStartMs) * 1000;
    for (const int speed_kt : {0, 50}) {
        CAPTURE(speed_kt);
        const std::vector<simulator::AirRecord> statuses = statuses_flown(speed_kt, 61);
        CHECK(statuses.size() >= 6);
        for (size_t i = 1; i < statuses.size(); i++)
            CHECK(statuses[i].at_us - statuses[i - 1].at_us <= 11000000 + kSlotUs);
        for (size_t i = 2; i < statuses.size(); i++) {
            CHECK(statuses[i].frequency_hz == statuses[i - 2].frequency_hz);
            CHECK(statuses[i].at_us - statuses[i - 2].at_us <= 20000000 + kSlotUs);
        }
    }
}

// The uplink carried here is the ground station's own, on the channel and in the slot G.3 names.
TEST_CASE(
    "ADS-L.4.SRD860.G.3: the Traffic Uplink rides the O-band HDR channel in the Uplink slot") {
    CHECK(adsl::kOBandHz == 869525000u);
    CHECK(adsl::kUplinkChipRateBps == 200000u);
    CHECK(timing::dwell_of(timing::kUplinkWindowMs).start_ms >= adsl::kUplinkSlotStartMs);
    CHECK(timing::dwell_of(timing::kUplinkWindowMs).end_ms <= adsl::kUplinkSlotEndMs);
    // A whole burst fits the window it is transmitted in.
    CHECK(timing::kGroundEmitStartMs + static_cast<int>(adsl::kUplinkBurstUs / 1000) <=
          timing::kGroundEmitEndMs);
}

// A ground station reaches every aircraft in range, so what it relays is checked, not trusted.
TEST_CASE("ADS-L.4.SRD860.G.3: a relayed target that is not an aircraft is refused, not drawn") {
    protocol::AdslUplink uplink;
    model::AircraftObservation targets[4] = {target(0x300001), target(0), target(0x300003),
                                             target(0x300004)};
    targets[3].position_valid = false;

    uint8_t packet[protocol::AdslUplink::kPacketBytes] = {0};
    REQUIRE(uplink.encode(targets, 4, kUplinkUtcS, 300, kStation,
                          adsl::kAddressTableManufacturerFirst, packet) == Status::Ok);

    model::AircraftObservation out[4]{};
    protocol::AdslUplink::DecodeStatistics statistics{};
    REQUIRE(uplink.decode(packet, sizeof(packet), kUplinkUtcS, 300, out, 4, statistics) ==
            Status::Ok);
    CHECK(statistics.targets == 2);
    CHECK(statistics.rejected == 2);
    for (int i = 0; i < statistics.targets; i++) CHECK(out[i].source == model::Source::AdslUplink);
}

// Encoded 300 ms into a second, the timestamp names 250 ms, so the record is 10 s old at 10.250.
TEST_CASE("ADS-L.4.SRD860.G.3: a relayed record older than 10 seconds is not drawn") {
    protocol::AdslUplink uplink;
    model::AircraftObservation targets[2] = {target(0x300001), target(0x300002)};
    uint8_t packet[protocol::AdslUplink::kPacketBytes] = {0};
    REQUIRE(uplink.encode(targets, 2, kUplinkUtcS, 300, kStation,
                          adsl::kAddressTableManufacturerFirst, packet) == Status::Ok);

    model::AircraftObservation out[2]{};
    protocol::AdslUplink::DecodeStatistics statistics{};
    REQUIRE(uplink.decode(packet, sizeof(packet), kUplinkUtcS + 10, 250, out, 2, statistics) ==
            Status::Ok);
    CHECK(statistics.targets == 2);
    REQUIRE(uplink.decode(packet, sizeof(packet), kUplinkUtcS + 10, 251, out, 2, statistics) ==
            Status::Ok);
    CHECK(statistics.targets == 0);
    CHECK(statistics.stale == 2);
    REQUIRE(uplink.decode(packet, sizeof(packet), kUplinkUtcS + 11, 300, out, 2, statistics) ==
            Status::Ok);
    CHECK(statistics.targets == 0);
    CHECK(statistics.stale == 2);
    REQUIRE(uplink.decode(packet, sizeof(packet), kUplinkUtcS - 1, 300, out, 2, statistics) ==
            Status::Ok);
    CHECK(statistics.stale == 2);
}

// What the ground station sends is the aircraft's own payload, so the climb and track a relay once
// lost now arrive.
TEST_CASE("ADS-L.4.SRD860.G.3: a relayed target arrives with its whole Traffic payload" *
          doctest::test_suite("main")) {
    protocol::AdslUplink uplink;
    model::AircraftObservation targets[1] = {target(0x300001)};
    targets[0].climb_valid = true;
    targets[0].climb_e8 = -20;
    targets[0].track_c9 = 300;
    uint8_t packet[protocol::AdslUplink::kPacketBytes] = {0};
    REQUIRE(uplink.encode(targets, 1, kUplinkUtcS, 300, kStation,
                          adsl::kAddressTableManufacturerFirst, packet) == Status::Ok);
    model::AircraftObservation out[1]{};
    protocol::AdslUplink::DecodeStatistics statistics{};
    REQUIRE(uplink.decode(packet, sizeof(packet), kUplinkUtcS, 300, out, 1, statistics) ==
            Status::Ok);
    REQUIRE(statistics.targets == 1);
    CHECK(out[0].address == 0x300001u);
    CHECK(out[0].climb_valid);
    CHECK(out[0].climb_e8 == -20);
    CHECK(out[0].track_c9 == 300);
}

// The packets come from reference.h's oracle, and the radio reads 230 bytes past the sync word.
TEST_CASE(
    "ADS-L.4.SRD860.G.3: a Traffic Uplink sent with the CRC is drawn, with one record or ten" *
    doctest::test_suite("main")) {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds(3, 25000, 900);
    adsl::Packet traffic[adsl::kUplinkMaxTargets];
    uint8_t packet[1 + adsl::kUplinkMaxLength] = {0};

    traffic[0] = relayed_near(rig, 0x3C2000, 1500);
    const int one = adsl::conformance::spec_uplink(traffic, 1, 0x01, false, packet);
    CHECK(one == 29);
    REQUIRE(hear_uplink(rig, packet, one));
    CHECK(rig.state().air.uplink_bad == 0);
    CHECK(rig.state().air.uplink_targets == 1);
    CHECK(rig.state().traffic.find(adsl::kAddressTableFlarm, 0x3C2000) >= 0);

    for (int i = 0; i < adsl::kUplinkMaxTargets; i++)
        traffic[i] = relayed_near(rig, 0x3C2100 + static_cast<uint32_t>(i), 500 + 300 * i);
    const int ten = adsl::conformance::spec_uplink(traffic, 10, 0x01, false, packet);
    CHECK(ten == 201);
    REQUIRE(hear_uplink(rig, packet, ten));
    CHECK(rig.state().air.uplink_frames == 2);
    CHECK(rig.state().air.uplink_bad == 0);
    CHECK(rig.state().air.uplink_targets == 11);
    CHECK(rig.state().traffic.count() == 11);
}
