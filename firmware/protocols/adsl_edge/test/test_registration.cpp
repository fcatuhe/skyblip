// OGN's payload 66 registration record, on ADS-L's frame: its layout, its version and its round
// trip.
#include <cstdint>
#include <cstring>

#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl_edge/registration.h"

using namespace skyblip;

TEST_CASE("adsl edge: a registration is payload 66, in the broadcast half of F.2.1's range") {
    adsl::Packet packet{};
    adsl_edge::encode_registration(packet, 0x123456, 58, "D-KXYZ", 6);
    CHECK(packet.payload_type == 66);
    CHECK(packet.payload_type < 0x80);
    CHECK_FALSE(packet.is_position());
    CHECK(adsl_edge::is_registration(packet));
    CHECK(packet.address() == 0x123456u);
    CHECK(packet.address_mapping_table() == 58);
}

// The header byte OGN reads: TelemType in the top two bits, InfoType in the low six.
TEST_CASE("adsl edge: the record is OGN's header byte 0x45, then 14 bytes of text") {
    adsl::Packet packet{};
    adsl_edge::encode_registration(packet, 0x123456, 58, "D-KXYZ", 6);
    CHECK(adsl_edge::telemetry_type(packet) == 1);
    CHECK(adsl_edge::information_type(packet) == 5);
    CHECK(packet.payload[adsl_edge::kInformationHeaderOffset] == 0x45);
    CHECK(std::memcmp(packet.payload + adsl_edge::kInformationTextOffset, "D-KXYZ", 6) == 0);
    for (int i = 6; i < adsl_edge::kInformationTextBytes; i++)
        CHECK(adsl_edge::information_text(packet)[i] == 0);
    CHECK(adsl_edge::kInformationTextOffset + adsl_edge::kInformationTextBytes ==
          adsl::Packet::kPayloadBytes);
}

// Issue 1 has no payload 66, so E.1.1's lowest version is Issue 2's.
TEST_CASE("adsl edge: a registration goes out as version 1") {
    adsl::Packet packet{};
    adsl_edge::encode_registration(packet, 0x123456, 58, "D-KXYZ", 6);
    CHECK(packet.protocol_version() == adsl::Packet::kProtocolVersionIssue2);
}

TEST_CASE("adsl edge: text past 14 bytes is cut, and a record of another type is no registration") {
    adsl::Packet packet{};
    adsl_edge::encode_registration(packet, 0x123456, 58, "ABCDEFGHIJKLMNOPQR", 18);
    CHECK(std::memcmp(adsl_edge::information_text(packet), "ABCDEFGHIJKLMN", 14) == 0);

    adsl_edge::set_information_header(packet, 0, 5);
    CHECK_FALSE(adsl_edge::is_registration(packet));
    packet.reset(adsl::Packet::kPayloadTypeStatus);
    adsl_edge::set_information_header(packet, 1, 5);
    CHECK_FALSE(adsl_edge::is_registration(packet));
}

TEST_CASE("adsl edge: a registration survives scrambling and the CRC, as every ADS-L frame does") {
    adsl::Packet sent{};
    adsl_edge::encode_registration(sent, 0x2ABCDE, 5, "F-JXYZ", 6);
    adsl::Packet air = sent;
    air.scramble();
    air.set_crc();
    const uint8_t erasures[adsl::Packet::kDataBytes] = {0};
    adsl::Packet heard;
    REQUIRE(adsl::receive(air.data, erasures, heard) == wire::Status::Ok);
    CHECK(adsl_edge::is_registration(heard));
    CHECK(std::memcmp(heard.data, sent.data, adsl::Packet::kCrcCoverBytes) == 0);
}
