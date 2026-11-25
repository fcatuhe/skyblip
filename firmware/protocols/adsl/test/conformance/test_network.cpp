// ADS-L 4 SRD-860 issue 2 Subpart E, the network layer: header, scrambler, parity and signature.
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/physical.h"
#include "protocols/adsl/reed_solomon.h"
#include "protocols/adsl/test/conformance/reference.h"
#include "protocols/adsl/uplink.h"
#include "protocols/common/manchester.h"
#include "protocols/common/xxtea.h"

using namespace skyblip;
using namespace skyblip::adsl::conformance;

namespace {
// The reference Traffic as another sender would put it on air under this network header.
wire::Status received_with_header(uint8_t header, adsl::Packet& out) {
    adsl::Packet sent = traffic_packet();
    sent.version = header;
    if (((header >> 5) & 0x03) == 0) sent.scramble();
    sent.set_crc();
    const uint8_t erasures[adsl::Packet::kDataBytes] = {0};
    return adsl::receive(sent.data, erasures, out);
}

}  // namespace

TEST_CASE("ADS-L.4.SRD860.E.1: the network header is one byte, and this one claims nothing") {
    adsl::Packet packet = traffic_packet();
    const uint8_t version = packet.version & 0x0F;
    const uint8_t signed_flag = (packet.version >> 4) & 0x01;
    const uint8_t key_index = (packet.version >> 5) & 0x03;
    const uint8_t error_control = (packet.version >> 7) & 0x01;
    CHECK(version == 0);
    CHECK(signed_flag == 0);
    CHECK(key_index == 0);
    CHECK(error_control == 0);
    CHECK(packet.version == 0x00);
}

// Issue 2 is version 1, but this payload encodes as Issue 1's did, and the clause wants the lowest.
TEST_CASE("ADS-L.4.SRD860.E.1.1: version 0 goes out, and a version 1 payload reads the same way") {
    adsl::Packet packet = traffic_packet();
    CHECK(packet.protocol_version() == 0);
    CHECK(adsl::lowest_version(packet) == 0);

    adsl::Traffic as_sent;
    REQUIRE(heard(packet, as_sent));

    adsl::Packet issue2 = packet;
    issue2.set_protocol_version(1);
    adsl::Traffic as_received;
    REQUIRE(heard(issue2, as_received));
    CHECK(as_received.address == as_sent.address);
    CHECK(as_received.latitude_1e7 == as_sent.latitude_1e7);
    CHECK(as_received.altitude_m == as_sent.altitude_m);
}

namespace {
adsl::Packet traffic_with(void (*change)(adsl::Traffic&)) {
    adsl::Traffic traffic = reference_traffic();
    change(traffic);
    adsl::Packet packet{};
    adsl::encode_traffic(packet, adsl::kAddressTableFlarm, traffic);
    return packet;
}
}  // namespace

// Issue 1 G.1.7 reads 0x3FFF as 61112 m, so an altitude marked invalid is Issue 2's code.
TEST_CASE("ADS-L.4.SRD860.E.1.1: an invalid altitude, 0x3FFF, goes out as version 1") {
    const adsl::Packet packet = traffic_with([](adsl::Traffic& t) { t.altitude_valid = false; });
    CHECK(packet.field(adsl::Packet::kAltitudeField) == 0x3FFFu);
    CHECK(packet.protocol_version() == 1);
    const adsl::Packet top = traffic_with([](adsl::Traffic& t) { t.altitude_m = 70000; });
    CHECK(top.field(adsl::Packet::kAltitudeField) == 0x3FFEu);
    CHECK(top.protocol_version() == 0);
}

// Issue 1 G.1.8 reads 0xFF as 238 m/s.
TEST_CASE("ADS-L.4.SRD860.E.1.1: an invalid ground speed, 0xFF, goes out as version 1") {
    const adsl::Packet packet = traffic_with([](adsl::Traffic& t) { t.speed_valid = false; });
    CHECK(packet.field(adsl::Packet::kSpeedField) == 0xFFu);
    CHECK(packet.protocol_version() == 1);
}

// Issue 1 G.1.9 reads 0x1FF as -119 m/s.
TEST_CASE("ADS-L.4.SRD860.E.1.1: an invalid vertical rate, 0x1FF, goes out as version 1") {
    const adsl::Packet packet = traffic_with([](adsl::Traffic& t) { t.climb_valid = false; });
    CHECK(packet.field(adsl::Packet::kClimbField) == 0x1FFu);
    CHECK(packet.protocol_version() == 1);
}

// Issue 1 G.1.5 marks no fix with 0xFFFFFF. Code 0x800000 falls outside the geographic bounds.
TEST_CASE("ADS-L.4.SRD860.E.1.1: a position marked absent, 0x800000, goes out as version 1") {
    const adsl::Packet packet = traffic_with([](adsl::Traffic& t) { t.position_valid = false; });
    CHECK(packet.field(adsl::Packet::kLatitudeField) == 0x800000u);
    CHECK(packet.protocol_version() == 1);
}

// Issue 1 G.1.3 reserves 14 to 31, where Issue 2 names four more kinds of aircraft.
TEST_CASE("ADS-L.4.SRD860.E.1.1: a category Issue 2 added, 14 to 17, goes out as version 1") {
    for (uint8_t category = 0; category < 32; category++) {
        adsl::Traffic traffic = reference_traffic();
        traffic.aircraft_category = category;
        adsl::Packet packet{};
        adsl::encode_traffic(packet, adsl::kAddressTableFlarm, traffic);
        CAPTURE(int(category));
        CHECK(packet.protocol_version() == (category >= 14 && category <= 17 ? 1 : 0));
    }
}

// Issue 1's payload table names Traffic, type 2, and reserves every other type.
TEST_CASE(
    "ADS-L.4.SRD860.E.1.1: a payload Issue 1 reserves, Status among them, goes out as version 1") {
    adsl::Packet status{};
    adsl::encode_status(status, 0x123456, 6, adsl::StatusReport{});
    CHECK(status.protocol_version() == 1);
    CHECK(adsl::lowest_version(status) == 1);
    adsl::Packet remote_id{};
    remote_id.reset(6);
    CHECK(adsl::lowest_version(remote_id) == 1);
}

// Issue 2's own Status says 1 is the most this device reads, so a later issue's payload is not
// read.
TEST_CASE("ADS-L.4.SRD860.E.1.1: a version above the highest this device reads is refused") {
    adsl::Packet packet;
    CHECK(received_with_header(0x01, packet) == wire::Status::Ok);
    CHECK(packet.protocol_version() == 1);
    CHECK(received_with_header(0x02, packet) == wire::Status::Unsupported);
    CHECK(received_with_header(0x0F, packet) == wire::Status::Unsupported);
}

// No signature promised, so the packet ends at its parity: 27 bytes of air, sync word included.
TEST_CASE("ADS-L.4.SRD860.E.1.2: no signature is promised, so nothing follows the parity") {
    adsl::Packet packet = traffic_packet();
    CHECK(((packet.version >> 4) & 0x01) == 0);
    CHECK(adsl::Packet::kTxBytes == 27);
    CHECK(adsl::Packet::kTxBytes == 3 + adsl::Packet::kDataBytes);
}

// D.1.1 leaves the signature out of the length, so a simple receiver reads the packet without it.
TEST_CASE("ADS-L.4.SRD860.E.1.2: a signed packet is read, and its signature left unread") {
    adsl::Packet packet;
    REQUIRE(received_with_header(0x10, packet) == wire::Status::Ok);
    adsl::Traffic traffic;
    REQUIRE(heard(packet, traffic));
    CHECK(traffic.address == 0x123456);
}

// "Low-power or constrained ADS-L clients may optionally use key index 3", so one is still heard.
TEST_CASE("ADS-L.4.SRD860.E.1.3: a packet sent under key 3 is read in the clear") {
    adsl::Packet packet;
    REQUIRE(received_with_header(0x60, packet) == wire::Status::Ok);
    adsl::Traffic traffic;
    REQUIRE(heard(packet, traffic));
    adsl::Traffic sent;
    REQUIRE(heard(traffic_packet(), sent));
    CHECK(traffic.address == 0x123456);
    CHECK(traffic.latitude_1e7 == sent.latitude_1e7);
    CHECK(traffic.altitude_m == sent.altitude_m);
}

TEST_CASE("ADS-L.4.SRD860.E.1.3: a packet under a reserved key, 1 or 2, is refused") {
    adsl::Packet packet;
    CHECK(received_with_header(0x20, packet) == wire::Status::Unsupported);
    CHECK(received_with_header(0x40, packet) == wire::Status::Unsupported);
}

TEST_CASE("ADS-L.4.SRD860.E.1.3: key 0 is the scrambling key, and it is the key used") {
    adsl::Packet packet = traffic_packet();
    CHECK(((packet.version >> 5) & 0x03) == 0);

    uint32_t words[5] = {0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u, 0x12345678u};
    uint32_t mirror[5];
    std::memcpy(mirror, words, sizeof(words));
    const uint32_t zero_key[4] = {0, 0, 0, 0};
    wire::xxtea_encrypt(words, 5, wire::kXxteaZeroKey, 6);
    spec_xxtea(mirror, 5, zero_key, 6);
    for (int i = 0; i < 5; i++) CHECK(words[i] == mirror[i]);
}

// E.2's table prints num_data_words 6, F.1 makes a Traffic packet 5 words, and 5 is what is
// scrambled.
TEST_CASE("ADS-L.4.SRD860.E.2: the scrambler is XXTEA over the ADS-L data, six rounds" *
          doctest::test_suite("main")) {
    adsl::Packet packet = traffic_packet();
    uint32_t mirror[5];
    std::memcpy(mirror, packet.payload, sizeof(mirror));
    const uint32_t zero_key[4] = {0, 0, 0, 0};
    spec_xxtea(mirror, 5, zero_key, 6);

    packet.scramble();
    CHECK(std::memcmp(packet.payload, mirror, sizeof(mirror)) == 0);
    CHECK(sizeof(mirror) % 4 == 0);

    packet.descramble();
    adsl::Packet again = traffic_packet();
    CHECK(std::memcmp(packet.payload, again.payload, 20) == 0);
}

// An M-band frame is checked by its CRC, so one whose header names the FEC contradicts itself.
TEST_CASE("ADS-L.4.SRD860.E.3: error control is the CRC unless the header bit names the FEC") {
    adsl::Packet packet;
    CHECK(received_with_header(0x00, packet) == wire::Status::Ok);
    CHECK(received_with_header(0x80, packet) == wire::Status::Invalid);
}

// G.3 pads the data to whole words, so a CRC length is a multiple of 4 and a FEC length 1 past one.
TEST_CASE(
    "ADS-L.4.SRD860.E.3: no uplink length is both a CRC and a FEC one, so it names the mode") {
    bool crc_length[256] = {false};
    bool fec_length[256] = {false};
    for (int count = 0; count <= 10; count++) {
        int payload = 19 * count;
        while ((payload - 3) % 4 != 0) payload++;
        const int covered = 1 + 5 + payload;
        crc_length[covered + 3] = true;
        fec_length[covered + 32] = true;
    }
    for (int length = 0; length < 256; length++) {
        CAPTURE(length);
        CHECK_FALSE((crc_length[length] && fec_length[length]));
        const adsl::UplinkLayout layout = adsl::uplink_layout(length);
        CHECK((layout.records >= 0) == (crc_length[length] || fec_length[length]));
        if (layout.records >= 0)
            CHECK((layout.mode == adsl::ErrorControl::Fec) == fec_length[length]);
    }
}

// The ground station picks the parity, so a header bit naming the other contradicts the length.
TEST_CASE("ADS-L.4.SRD860.E.3: an uplink whose header bit names the other parity is refused") {
    const adsl::Packet traffic[1] = {traffic_packet(0x3C1234)};
    adsl::Packet heard[1];
    adsl::UplinkStatistics statistics;
    uint8_t packet[1 + adsl::kUplinkMaxLength] = {0};
    const int crc_sent = spec_uplink(traffic, 1, 0x81, false, packet);
    CHECK(packet[0] == 28);
    CHECK(adsl::Uplink{}.decode(packet, static_cast<size_t>(crc_sent), heard, 1, statistics) ==
          wire::Status::Invalid);
    const int fec_sent = spec_uplink(traffic, 1, 0x01, true, packet);
    CHECK(packet[0] == 57);
    CHECK(adsl::Uplink{}.decode(packet, static_cast<size_t>(fec_sent), heard, 1, statistics) ==
          wire::Status::Invalid);
}

// Four header fields in one byte, and this device sends a zero in every one of them.
// The Mode-S polynomial over the header and the scrambled data, most significant byte first.
TEST_CASE("ADS-L.4.SRD860.E.3.1: the parity is the 24-bit CRC the clause prints" *
          doctest::test_suite("main")) {
    adsl::Packet packet = traffic_packet();
    packet.scramble();
    packet.set_crc();

    const uint32_t expected = spec_crc24(packet.data, adsl::Packet::kCrcCoverBytes);
    CHECK(packet.crc_bytes[0] == static_cast<uint8_t>(expected >> 16));
    CHECK(packet.crc_bytes[1] == static_cast<uint8_t>(expected >> 8));
    CHECK(packet.crc_bytes[2] == static_cast<uint8_t>(expected));
    CHECK(packet.crc_ok());
    CHECK(adsl::Packet::kCrcCoverBytes == 21);
}

// One bit wrong in the header, the data or the CRC itself, and a CRC uplink is refused.
TEST_CASE("ADS-L.4.SRD860.E.3.1: an uplink whose CRC does not check is refused") {
    const adsl::Packet traffic[1] = {traffic_packet(0x3C1234)};
    for (const int byte : {1, 2, 25, 26, 28}) {
        CAPTURE(byte);
        uint8_t packet[1 + adsl::kUplinkMaxLength] = {0};
        const int sent = spec_uplink(traffic, 1, 0x01, false, packet);
        packet[byte] ^= 0x04;
        adsl::Packet heard[1];
        adsl::UplinkStatistics statistics;
        CHECK(adsl::Uplink{}.decode(packet, static_cast<size_t>(sent), heard, 1, statistics) ==
              wire::Status::Crc);
    }
}

TEST_CASE("ADS-L.4.SRD860.E.3.2: the forward error correction is Reed-Solomon over 255 bytes") {
    CHECK(adsl::ReedSolomon255::kCodewordBytes == 255);
    CHECK(adsl::ReedSolomon255::kDataBytes == 223);
    CHECK(adsl::ReedSolomon255::kParityBytes == 32);
    CHECK(adsl::ReedSolomon255::kMaxErrors == 16);
}

// The clause's own parameters, worked bit by bit: no table is shared with the stack's coder.
TEST_CASE("ADS-L.4.SRD860.E.3.2: the code is generated from the roots the clause names") {
    uint8_t data[adsl::ReedSolomon255::kDataBytes];
    for (int i = 0; i < adsl::ReedSolomon255::kDataBytes; i++)
        data[i] = static_cast<uint8_t>(i * 29 + 7);
    uint8_t expected[adsl::ReedSolomon255::kParityBytes];
    spec_reed_solomon(data, expected);
    uint8_t parity[adsl::ReedSolomon255::kParityBytes];
    adsl::ReedSolomon255{}.encode(data, parity);
    CHECK(std::memcmp(parity, expected, sizeof(parity)) == 0);
}

// The parity of 200 zeros then 1 to 23, as the reedsolo library computes it with fcr 121 and
// generator 232.
TEST_CASE("ADS-L.4.SRD860.E.3.2: a short block is prefixed with zeros to 223 bytes") {
    const uint8_t kExpected[adsl::ReedSolomon255::kParityBytes] = {
        0xC1, 0x8D, 0xFB, 0x52, 0xD3, 0x0D, 0x57, 0xF7, 0x27, 0xE0, 0x99,
        0xF7, 0x2B, 0x87, 0x2B, 0xB2, 0x0F, 0x36, 0xE9, 0xAA, 0xA6, 0x71,
        0xE0, 0x4F, 0x10, 0xF5, 0x9D, 0xE6, 0x90, 0x71, 0xDA, 0x95};
    uint8_t data[adsl::ReedSolomon255::kDataBytes] = {0};
    for (int i = 0; i < 23; i++) data[200 + i] = static_cast<uint8_t>(i + 1);
    uint8_t parity[adsl::ReedSolomon255::kParityBytes];
    adsl::ReedSolomon255{}.encode(data, parity);
    CHECK(std::memcmp(parity, kExpected, sizeof(parity)) == 0);
    CHECK(adsl::ReedSolomon255::kPrimitiveElement == 11);
    CHECK(adsl::ReedSolomon255::kFirstRoot == 121);
    CHECK(adsl::ReedSolomon255::kFieldPolynomial == 0x11D);
}

// Optional, and not taken: no key is held, so no packet is signed and none claims to be.
TEST_CASE("ADS-L.4.SRD860.E.4: nothing is signed, and nothing says it is") {
    adsl::Packet packet = traffic_packet();
    packet.scramble();
    packet.set_crc();
    CHECK(((packet.version >> 4) & 0x01) == 0);
    CHECK(adsl::kFrameBytes == adsl::Packet::kDataBytes);

    uint8_t chips[kTxChipBytes] = {0};
    const size_t chip_bytes =
        wire::manchester_frame(adsl::kSyncWord, packet.data, adsl::kFrameBytes, chips);
    CHECK(chip_bytes == 2u * (wire::kSyncWordBytes + adsl::kFrameBytes));
}

// INFO: fc 13nov26 optional: E.4 says ADS-L clients "may optionally" sign, and this one does not
TEST_CASE("ADS-L.4.SRD860.E.4.1: a signature carries the UTC second it was made in" *
          doctest::skip()) {
    FAIL("optional: no secure signature is produced, so there is no timestamp field to fill");
}

// INFO: fc 13nov26 optional: E.4 says clients "may optionally" sign, so no Ed25519 key is held
TEST_CASE("ADS-L.4.SRD860.E.4.2: the signature is Ed25519 over the header, the data and the time" *
          doctest::skip()) {
    FAIL("optional: no Ed25519 key is held, so nothing signs a packet");
}
