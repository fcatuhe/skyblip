// ADS-L 4 SRD-860 issue 2 G.2 to G.5: the Status, Traffic Uplink, FIS-B and Remote ID payloads.
#include <cstdint>
#include <vector>

#include "core/comms/peer.h"
#include "core/gnss/first_fix.h"
#include "core/model/aircraft.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/settings/address.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "protocols/common/bit_field.h"
#include "simulator/paired_simulator.h"
#include "simulator/simulator.h"
#include "test/support/simulator_run.h"

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

// The dwells hear 868.2 MHz from 450 to 798 ms and 868.4 MHz from 800 to 1000, of the direct slot's
// 550.
TEST_CASE("ADS-L.4.SRD860.G.2.2: the Status payload declares the M-band dwell as a receive class") {
    CHECK(timing::Scheduler::direct_heard_permille() == (348 + 200) * 1000 / 1100);
    CHECK(status_field(wire::BitField{44, 2}) == 2);
}

TEST_CASE("ADS-L.4.SRD860.G.2.3: the Status payload declares the O-band LDR receive class") {
    CHECK(status_field(wire::BitField{46, 2}) == 0);
}

// The O-band dwell sits in the uplink slot, which the clause says does not count.
TEST_CASE("ADS-L.4.SRD860.G.2.4: the Status payload declares the O-band HDR receive class") {
    CHECK(timing::kUplinkRxEndMs <= adsl::kDirectStartMs);
    CHECK(status_field(wire::BitField{48, 2}) == 0);
}

TEST_CASE("ADS-L.4.SRD860.G.2.5: the Status payload declares the ADS-L 4 Mobile state") {
    CHECK(status_field(wire::BitField{50, 2}) == 0);
}

// Bit 0 is the FLARM-generation wire this device decodes, bit 4 the ADS-B a paired Bravo hears.
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

TEST_CASE("ADS-L.4.SRD860.G.2.7: the Status payload declares which other systems are transmitted") {
    CHECK(status_field(wire::BitField{60, 8}) == 0);
}

TEST_CASE("ADS-L.4.SRD860.G.2.8: the Status payload declares the transponder fitted") {
    CHECK(status_field(wire::BitField{68, 2}) == 0);
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
TEST_CASE("ADS-L.4.SRD860.G.2.11: a Status payload goes out at least every 20 seconds") {
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
    CHECK(timing::kUplinkRxStartMs >= 200);
    CHECK(timing::kUplinkRxEndMs <= 450);
    // A whole burst fits the slot it is transmitted in, with the dwell's guard either side.
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

// The device dates each relayed record by the second it heard the packet in.
TEST_CASE("ADS-L.4.SRD860.G.3: a relayed record older than 10 seconds is not drawn") {
    protocol::AdslUplink uplink;
    model::AircraftObservation targets[2] = {target(0x300001), target(0x300002)};
    uint8_t packet[protocol::AdslUplink::kPacketBytes] = {0};
    REQUIRE(uplink.encode(targets, 2, kUplinkUtcS, 300, kStation,
                          adsl::kAddressTableManufacturerFirst, packet) == Status::Ok);

    model::AircraftObservation out[2]{};
    protocol::AdslUplink::DecodeStatistics statistics{};
    REQUIRE(uplink.decode(packet, sizeof(packet), kUplinkUtcS + 10, 300, out, 2, statistics) ==
            Status::Ok);
    CHECK(statistics.targets == 2);
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
TEST_CASE("ADS-L.4.SRD860.G.3: a relayed target arrives with its whole Traffic payload") {
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
