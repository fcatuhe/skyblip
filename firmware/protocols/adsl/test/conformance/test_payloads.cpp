// ADS-L 4 SRD-860 issue 2 G.2 to G.5: the Status payload the component writes, and the payloads it
// does not.
#include <cstdint>
#include <cstring>

#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/reed_solomon.h"
#include "protocols/adsl/test/conformance/reference.h"
#include "protocols/adsl/transmit.h"
#include "protocols/adsl/uplink.h"
#include "protocols/common/bit_field.h"
#include "protocols/common/xxtea.h"

using namespace skyblip;

namespace {
adsl::Packet relayed_traffic(uint32_t address, uint8_t timestamp) {
    adsl::Traffic traffic = adsl::conformance::reference_traffic();
    traffic.address = address;
    traffic.timestamp = timestamp;
    adsl::Packet packet{};
    adsl::encode_traffic(packet, traffic);
    return packet;
}

void sent_uplink(const adsl::Packet* traffic, int count, uint8_t* packet) {
    REQUIRE(adsl::Uplink{}.encode(0x00FFFE, adsl::kAddressTableManufacturerFirst, traffic, count,
                                  packet) == wire::Status::Ok);
}
}  // namespace

// The payload is read back by hand from the scrambled bytes, at G.3's own offsets.
TEST_CASE(
    "ADS-L.4.SRD860.G.3: an uplink record is a 30-bit address and the Traffic payload, "
    "verbatim") {
    adsl::Packet traffic[2] = {relayed_traffic(0x3C1234, 8), relayed_traffic(0x3C5678, 12)};
    uint8_t packet[1 + adsl::kUplinkMaxLength];
    sent_uplink(traffic, 2, packet);

    const int data_bytes = adsl::kUplinkHeaderBytes + adsl::uplink_payload_bytes(2);
    uint32_t words[(adsl::kUplinkHeaderBytes + 191) / 4];
    std::memcpy(words, packet + 2, static_cast<size_t>(data_bytes));
    wire::xxtea_decrypt(words, static_cast<uint32_t>(data_bytes / 4), wire::kXxteaZeroKey, 6);
    uint8_t data[adsl::kUplinkHeaderBytes + 191];
    std::memcpy(data, words, static_cast<size_t>(data_bytes));

    CHECK(data[0] == 4);
    for (int i = 0; i < 2; i++) {
        const uint8_t* record = data + adsl::kUplinkHeaderBytes + 19 * i;
        const uint32_t address_word = wire::read_bits(record, {0, 30});
        CHECK(address_word == wire::read_bits(traffic[i].payload + 1, {0, 30}));
        CHECK(wire::read_bits(record, {30, 2}) == 0);
        CHECK(std::memcmp(record + 4, traffic[i].payload + 5, 15) == 0);
    }
    CHECK(adsl::uplink_payload_bytes(1) == 19);
    CHECK(adsl::uplink_payload_bytes(2) == 39);
    CHECK(adsl::uplink_payload_bytes(10) == 191);
    CHECK(adsl::kUplinkMaxTargets == 10);
}

// The receiver counts records from D.1.1's length byte, since nothing else in the packet says.
TEST_CASE("ADS-L.4.SRD860.G.3: the number of targets is read from the Packet Length field") {
    for (int count = 0; count <= adsl::kUplinkMaxTargets; count++) {
        adsl::Packet traffic[adsl::kUplinkMaxTargets];
        for (int i = 0; i < count; i++)
            traffic[i] = relayed_traffic(0x100000u + static_cast<uint32_t>(i), 0);
        uint8_t packet[1 + adsl::kUplinkMaxLength];
        sent_uplink(traffic, count, packet);
        CHECK(packet[0] == adsl::uplink_length(count));

        adsl::Packet heard[adsl::kUplinkMaxTargets];
        adsl::UplinkStatistics statistics;
        REQUIRE(adsl::Uplink{}.decode(packet, sizeof(packet), heard, adsl::kUplinkMaxTargets,
                                      statistics) == wire::Status::Ok);
        CHECK(statistics.targets == count);
    }
}

// G.1.1 counts quarter seconds inside 15 s, so a record dated 40 quarters ago is exactly 10 s old.
TEST_CASE("ADS-L.4.SRD860.G.3: a relayed Traffic payload older than 10 seconds is not forwarded") {
    const uint32_t cycle_start_s = 1785628815;
    const adsl::Packet record = relayed_traffic(0x3C1234, 0);
    CHECK(adsl::uplink_record_current(record, cycle_start_s + 10, 0));
    CHECK_FALSE(adsl::uplink_record_current(record, cycle_start_s + 10, 250));
    CHECK_FALSE(adsl::uplink_record_current(record, cycle_start_s + 14, 750));
}

TEST_CASE("ADS-L.4.SRD860.G.3: a relayed timestamp may not be in the future") {
    const uint32_t cycle_start_s = 1785628815;
    const adsl::Packet record = relayed_traffic(0x3C1234, 4);
    CHECK_FALSE(adsl::uplink_record_current(record, cycle_start_s, 0));
    CHECK_FALSE(adsl::uplink_record_current(record, cycle_start_s, 750));
    CHECK(adsl::uplink_record_current(record, cycle_start_s + 1, 0));
}

// The header byte is in clear, and its error control bit says the parity is E.3.2's.
TEST_CASE("ADS-L.4.SRD860.G.3: the uplink is sent with FEC, as an Issue 2 payload") {
    adsl::Packet traffic[1] = {relayed_traffic(0x3C1234, 0)};
    uint8_t packet[1 + adsl::kUplinkMaxLength];
    sent_uplink(traffic, 1, packet);
    CHECK((packet[1] & 0x0F) == adsl::Packet::kProtocolVersionIssue2);
    CHECK(((packet[1] >> 7) & 1) == 1);
    CHECK(((packet[1] >> 4) & 1) == 0);

    const int covered = 1 + adsl::kUplinkHeaderBytes + 19;
    uint8_t block[adsl::ReedSolomon255::kDataBytes] = {0};
    std::memcpy(block + adsl::ReedSolomon255::kDataBytes - covered, packet + 1, covered);
    uint8_t parity[adsl::ReedSolomon255::kParityBytes];
    adsl::ReedSolomon255{}.encode(block, parity);
    CHECK(std::memcmp(packet + 1 + covered, parity, sizeof(parity)) == 0);
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

// Every field of the payload table, written from a report that sets each one apart.
TEST_CASE(
    "ADS-L.4.SRD860.G.2: a Status payload is type 3, Issue 2, with its fields where the table puts "
    "them") {
    adsl::StatusReport report;
    report.m_band = adsl::ReceiveClass::Partial;
    report.o_band_ldr = adsl::ReceiveClass::Occasional;
    report.o_band_hdr = adsl::ReceiveClass::Full;
    report.conspicuity_received = adsl::conspicuity::kFlarm | adsl::conspicuity::kAdsb1090;
    report.conspicuity_transmitted = 0x5A;
    report.traffic_uplink_client = true;
    adsl::Packet packet{};
    adsl::encode_status(packet, 0x2ABCDE, 58, report);

    CHECK(packet.payload_type == 3);
    CHECK((packet.version & 0x0F) == 1);
    CHECK(packet.address() == 0x2ABCDEu);
    CHECK(packet.address_mapping_table() == 58);
    CHECK(wire::read_bits(packet.payload, wire::BitField{40, 4}) == 1);
    CHECK(wire::read_bits(packet.payload, wire::BitField{44, 2}) == 2);
    CHECK(wire::read_bits(packet.payload, wire::BitField{46, 2}) == 1);
    CHECK(wire::read_bits(packet.payload, wire::BitField{48, 2}) == 3);
    CHECK(wire::read_bits(packet.payload, wire::BitField{50, 2}) == 0);
    CHECK(wire::read_bits(packet.payload, wire::BitField{52, 8}) == 0x11);
    CHECK(wire::read_bits(packet.payload, wire::BitField{60, 8}) == 0x5A);
    CHECK(wire::read_bits(packet.payload, wire::BitField{68, 2}) == 0);
    CHECK(wire::read_bits(packet.payload, wire::BitField{70, 1}) == 1);
    CHECK(wire::read_bits(packet.payload, wire::BitField{71, 1}) == 0);
    CHECK(wire::read_bits(packet.payload, wire::BitField{72, 24}) == 0);
}

// The clause's classes are shares of the direct slot: more than 5%, 40% and 80%.
TEST_CASE(
    "ADS-L.4.SRD860.G.2.2: a receive class is the share of the direct slot a receiver hears") {
    CHECK(adsl::receive_class(50) == adsl::ReceiveClass::None);
    CHECK(adsl::receive_class(51) == adsl::ReceiveClass::Occasional);
    CHECK(adsl::receive_class(400) == adsl::ReceiveClass::Occasional);
    CHECK(adsl::receive_class(401) == adsl::ReceiveClass::Partial);
    CHECK(adsl::receive_class(800) == adsl::ReceiveClass::Partial);
    CHECK(adsl::receive_class(801) == adsl::ReceiveClass::Full);
}

// Where in those 20 s a unit sends it is the integrator's choice, and the device half checks
// skyBlip's.
TEST_CASE("ADS-L.4.SRD860.G.2.11: the Status period the clause sets is 20 seconds") {
    CHECK(adsl::kStatusPeriodS == 20u);
}
