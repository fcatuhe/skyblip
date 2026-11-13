// ADS-L 4 SRD-860 issue 2 G.2 to G.5: the Status payload the component writes, and the payloads it
// does not.
#include <cstdint>

#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/transmit.h"
#include "protocols/adsl/uplink.h"
#include "protocols/common/bit_field.h"

using namespace skyblip;

// TODO: fc 18sep26 the uplink frame is a house format: 16-byte records, no verbatim payload
TEST_CASE(
    "ADS-L.4.SRD860.G.3: an uplink record is a 30-bit address and the Traffic payload, "
    "verbatim" *
    doctest::skip()) {
    const int record_bits = 30 + 2 + 120;
    CHECK(record_bits == 152);
    CHECK(adsl::Uplink::kRecordBytes * 8 == record_bits);
    CHECK(adsl::Uplink::kMaxTargets == 10);
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
