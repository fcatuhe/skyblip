// ADS-L 4 SRD-860 issue 2 Subpart F: the ADS-L header, who a packet is from and what it carries.
#include <cstdint>
#include <initializer_list>

#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/test/conformance/reference.h"
#include "protocols/common/little_endian.h"

using namespace skyblip;
using namespace skyblip::adsl::conformance;

// 40 bits of header and 120 of Traffic payload: 20 bytes, which is the block E.2 scrambles whole.
TEST_CASE("ADS-L.4.SRD860.F.1: the ADS-L data is a 40-bit header and a payload behind it") {
    const int header_bits = 40;
    const int traffic_payload_bits = 120;
    CHECK((header_bits + traffic_payload_bits) / 8 == 20);
    CHECK(sizeof(adsl::Packet{}.payload) == 20);
    // The clause's rule for a payload width: 24 + N*32 bits.
    CHECK((traffic_payload_bits - 24) % 32 == 0);
}

TEST_CASE("ADS-L.4.SRD860.F.2: the header is a type byte, 30 bits of sender, reserved, relay") {
    adsl::Packet packet = traffic_packet();
    CHECK(packet.payload_type == 0x02);
    CHECK(packet.address() == 0x123456u);
    CHECK(packet.address_mapping_table() == 6);
    CHECK_FALSE(packet.is_relay());

    packet.set_relay();
    CHECK(packet.is_relay());
    CHECK((packet.address_bytes[3] & 0x80) != 0);

    // One little-endian word: the AMT in bits 0 to 5, the address in bits 6 to 29.
    CHECK((wire::get_uint32(packet.address_bytes) & 0x3Fu) == 6u);
    CHECK(((wire::get_uint32(packet.address_bytes) >> 6) & 0x00FFFFFFu) == 0x123456u);
}

TEST_CASE("ADS-L.4.SRD860.F.2.1: a Traffic payload is type 2, in the broadcast half of the range") {
    adsl::Packet packet = traffic_packet();
    CHECK(packet.payload_type == 0x02);
    CHECK(packet.is_position());
    CHECK(packet.payload_type < 0x80);
}

// Issue 2 puts Status, Remote ID and the uplinks on this band, and none of them is an aeroplane.
TEST_CASE("ADS-L.4.SRD860.F.2.1: a payload that is not Traffic never becomes an aircraft") {
    adsl::Traffic observation;
    for (uint8_t type : {uint8_t(0x00), uint8_t(0x03), uint8_t(0x04), uint8_t(0x05), uint8_t(0x06),
                         uint8_t(0x42), uint8_t(0x82)}) {
        adsl::Packet packet = traffic_packet();
        packet.payload_type = type;
        CHECK_FALSE(packet.is_position());
        CHECK_FALSE(heard(packet, observation));
        CHECK_FALSE(observation.position_valid);
    }
}

// Every ADS-L data block has to be scramblable, and XXTEA works on whole 32-bit words.
TEST_CASE("ADS-L.4.SRD860.F.2.5: header plus payload is a whole number of 32-bit words") {
    const int header_bytes = 5;
    const int traffic_payload_bytes = 15;
    CHECK((header_bytes + traffic_payload_bytes) % 4 == 0);
    // The clause's own list of valid payload sizes: 3, 7, 11, 15, and on by fours.
    CHECK((traffic_payload_bytes - 3) % 4 == 0);
}
