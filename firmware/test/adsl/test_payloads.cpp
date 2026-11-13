// ADS-L 4 SRD-860 issue 2 G.2 to G.5: the Status, Traffic Uplink, FIS-B and Remote ID payloads.
#include <cstdint>

#include "core/events/central.h"
#include "core/model/aircraft.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/settings/address.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "core/util/bit_field.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/identity.h"
#include "simulator/simulator.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {
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
    return observation;
}
// What a simulated Go puts on air as its Status, read back at G.2's bit offsets.
protocol::AdslPacket status_on_air(bool bravo_paired) {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    if (bravo_paired)
        simulator.product().settings().bravo = events::BluetoothAddress{0, {1, 2, 3, 4, 5, 6}};
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    run_on(simulator, past_settling(simulator), (timing::Transmitter::kStatusPeriodS + 1) * 1000);
    const simulator::Air& air = simulator.world().air();
    for (int i = 0; i < air.record_count(); i++) {
        protocol::AdslPacket packet{};
        if (packet_of(air.record_at(i), packet) &&
            packet.payload_type == protocol::AdslPacket::kPayloadTypeStatus)
            return packet;
    }
    FAIL("no Status burst in twenty-one seconds of flight");
    return protocol::AdslPacket{};
}

const protocol::AdslPacket& status_heard() {
    static const protocol::AdslPacket packet = status_on_air(false);
    return packet;
}

uint32_t status_field(BitField where) { return status_heard().field(where); }

}  // namespace

TEST_CASE(
    "ADS-L.4.SRD860.G.2: a Status payload declares this device's bands, systems and version") {
    const protocol::AdslPacket& packet = status_heard();
    CHECK(packet.payload_type == 3);
    CHECK(packet.address() == settings::address_of(platform::host::Identity::kSerial));
    // E.1.1: the Status payload is Issue 2's, so its network header says version 1.
    CHECK((packet.version & 0x0F) == 1);
    // 24 reserved bits close the 56 the payload table lists.
    CHECK(read_bits(packet.payload, BitField{72, 24}) == 0);
}

TEST_CASE("ADS-L.4.SRD860.G.2.1: the Status payload names the highest version this device reads") {
    CHECK(status_field(BitField{40, 4}) == 1);
}

// The dwells hear 868.2 MHz from 450 to 798 ms and 868.4 MHz from 800 to 1000, of the direct slot's
// 550.
TEST_CASE("ADS-L.4.SRD860.G.2.2: the Status payload declares the M-band dwell as a receive class") {
    CHECK(timing::Scheduler::direct_heard_permille() == (348 + 200) * 1000 / 1100);
    CHECK(status_field(BitField{44, 2}) == 2);
}

TEST_CASE("ADS-L.4.SRD860.G.2.3: the Status payload declares the O-band LDR receive class") {
    CHECK(status_field(BitField{46, 2}) == 0);
}

// The O-band dwell sits in the uplink slot, which the clause says does not count.
TEST_CASE("ADS-L.4.SRD860.G.2.4: the Status payload declares the O-band HDR receive class") {
    CHECK(timing::kUplinkRxEndMs <= timing::kDirectStartMs);
    CHECK(status_field(BitField{48, 2}) == 0);
}

TEST_CASE("ADS-L.4.SRD860.G.2.5: the Status payload declares the ADS-L 4 Mobile state") {
    CHECK(status_field(BitField{50, 2}) == 0);
}

// Bit 0 is the FLARM-generation wire this device decodes, bit 4 the ADS-B a paired Bravo hears.
TEST_CASE("ADS-L.4.SRD860.G.2.6: the Status payload declares which other systems are received") {
    CHECK(status_field(BitField{52, 8}) == 0x01);
    CHECK(status_on_air(true).field(BitField{52, 8}) == 0x11);
}

TEST_CASE("ADS-L.4.SRD860.G.2.7: the Status payload declares which other systems are transmitted") {
    CHECK(status_field(BitField{60, 8}) == 0);
}

TEST_CASE("ADS-L.4.SRD860.G.2.8: the Status payload declares the transponder fitted") {
    CHECK(status_field(BitField{68, 2}) == 0);
}

TEST_CASE("ADS-L.4.SRD860.G.2.9: the Status payload declares that Traffic Uplink is processed") {
    CHECK(status_field(BitField{70, 1}) == 1);
}

TEST_CASE("ADS-L.4.SRD860.G.2.10: the Status payload declares whether FIS-B is processed") {
    CHECK(status_field(BitField{71, 1}) == 0);
}

TEST_CASE("ADS-L.4.SRD860.G.2.11: a Status payload goes out at least every 20 seconds") {
    timing::Transmitter transmitter;
    transmitter.configure(0x5B7E57);
    uint32_t last_s = 0;
    for (uint32_t utc_s = 1785628800; utc_s < 1785628800 + 100; utc_s++) {
        if (utc_s % timing::Transmitter::kStatusPeriodS != transmitter.status_second()) continue;
        if (last_s != 0) CHECK(utc_s - last_s <= 20);
        last_s = utc_s;
    }
    CHECK(last_s != 0);
}

// The uplink carried here is the ground station's own, on the channel and in the slot G.3 names.
TEST_CASE(
    "ADS-L.4.SRD860.G.3: the Traffic Uplink rides the O-band HDR channel in the Uplink slot") {
    CHECK(timing::kOBandHz == 869525000u);
    CHECK(protocol::kUplinkChipRateBps == 200000u);
    CHECK(timing::kUplinkRxStartMs >= 200);
    CHECK(timing::kUplinkRxEndMs <= 450);
    // A whole burst fits the slot it is transmitted in, with the dwell's guard either side.
    CHECK(timing::kGroundEmitStartMs + static_cast<int>(protocol::kUplinkBurstUs / 1000) <=
          timing::kGroundEmitEndMs);
}

// A ground station reaches every aircraft in range, so what it relays is checked, not trusted.
TEST_CASE("ADS-L.4.SRD860.G.3: a relayed target that is not an aircraft is refused, not drawn") {
    protocol::AdslUplink uplink;
    model::AircraftObservation targets[4] = {target(0x300001), target(0), target(0x300003),
                                             target(0x300004)};
    targets[3].latitude_1e7 = 1800000000;

    uint8_t frame[protocol::AdslUplink::kFrameBytes] = {0};
    REQUIRE(uplink.encode(targets, 4, 0, frame) == Status::Ok);

    model::AircraftObservation out[4]{};
    protocol::AdslUplink::DecodeStatistics statistics{};
    REQUIRE(uplink.decode(frame, out, 4, statistics) == Status::Ok);
    CHECK(statistics.targets == 2);
    CHECK(statistics.rejected == 2);
    for (int i = 0; i < statistics.targets; i++) CHECK(out[i].source == model::Source::AdslUplink);
}

// TODO: fc 18sep26 the uplink frame is a house format: 16-byte records, no verbatim payload
TEST_CASE(
    "ADS-L.4.SRD860.G.3: an uplink record is a 30-bit address and the Traffic payload, "
    "verbatim" *
    doctest::skip()) {
    const int record_bits = 30 + 2 + 120;
    CHECK(record_bits == 152);
    CHECK(protocol::AdslUplink::kRecordBytes * 8 == record_bits);
    CHECK(protocol::AdslUplink::kMaxTargets == 10);
}

// TODO: fc 18sep26 an uplink record carries no timestamp, so its age cannot be checked at all
TEST_CASE("ADS-L.4.SRD860.G.3: a relayed Traffic payload older than 10 seconds is not forwarded" *
          doctest::skip()) {
    FAIL("no timestamp travels with a relayed target, so nothing here can refuse a stale one");
}

// TODO: fc 18sep26 no FIS-B: DO-358B ground uplink messages are neither received nor decoded
TEST_CASE("ADS-L.4.SRD860.G.4: a FIS-B Uplink payload carries a DO-358B ground uplink message" *
          doctest::skip()) {
    FAIL("the FIS-B payload is not implemented: type 5 is dropped with every other unknown type");
}

// TODO: fc 18sep26 no Remote Identification: a drone payload this aircraft device never sends
TEST_CASE("ADS-L.4.SRD860.G.5: a Remote Identification payload carries an EN 4709-002 block" *
          doctest::skip()) {
    FAIL("the Remote Identification payload is not implemented: type 6 is dropped as unknown");
}
