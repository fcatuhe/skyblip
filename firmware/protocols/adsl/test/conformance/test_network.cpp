// ADS-L 4 SRD-860 issue 2 Subpart E, the network layer: header, scrambler, parity and signature.
#include <cstdint>
#include <cstring>

#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/physical.h"
#include "protocols/adsl/reed_solomon.h"
#include "protocols/adsl/test/conformance/reference.h"
#include "protocols/common/manchester.h"
#include "protocols/common/xxtea.h"

using namespace skyblip;
using namespace skyblip::adsl::conformance;

namespace {
// E.2's own sample implementation, transcribed, with the zero key the clause names for scrambling.
void spec_xxtea(uint32_t* data, uint32_t words, const uint32_t* key, uint32_t rounds) {
    const uint32_t kDelta = 0x9E3779B9u;
    uint32_t next = data[0];
    uint32_t sum = 0;
    uint32_t previous = data[words - 1];
    uint32_t rounds_left = rounds;
    while (rounds_left-- > 0) {
        sum += kDelta;
        const uint32_t key_offset = (sum >> 2) & 3u;
        uint32_t index = 0;
        for (index = 0; index < words - 1; index++) {
            next = data[index + 1];
            data[index] += (((previous >> 5) ^ (next << 2)) + ((next >> 3) ^ (previous << 4))) ^
                           ((sum ^ next) + (key[(index & 3u) ^ key_offset] ^ previous));
            previous = data[index];
        }
        next = data[0];
        data[words - 1] += (((previous >> 5) ^ (next << 2)) + ((next >> 3) ^ (previous << 4))) ^
                           ((sum ^ next) + (key[(index & 3u) ^ key_offset] ^ previous));
        previous = data[words - 1];
    }
}

// E.3.1's own sample implementation, transcribed.
uint32_t spec_crc24(const uint8_t* data, size_t bytes) {
    const uint32_t kPolynomial = 0xFFFA0480u;
    uint32_t crc = 0;
    for (size_t i = 0; i < bytes + 3; i++) {
        crc |= i < bytes ? data[i] : 0u;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80000000u) crc ^= kPolynomial;
            crc <<= 1;
        }
    }
    return crc >> 8;
}

// INFO: fc 13nov26 E.3.2 transcribed, reading element 11 as alpha^11 the way CCSDS 131.0 does
uint8_t spec_multiply(uint8_t left, uint8_t right) {
    uint8_t product = 0;
    while (right != 0) {
        if (right & 1u) product ^= left;
        const bool carry = (left & 0x80u) != 0;
        left = static_cast<uint8_t>(left << 1);
        if (carry) left ^= 0x1Du;
        right = static_cast<uint8_t>(right >> 1);
    }
    return product;
}

uint8_t spec_power(uint8_t base, int exponent) {
    uint8_t result = 1;
    for (int i = 0; i < exponent; i++) result = spec_multiply(result, base);
    return result;
}

void spec_reed_solomon(const uint8_t* data, uint8_t* parity) {
    const int kParity = 32;
    uint8_t generator[kParity + 1] = {1};
    for (int i = 0; i < kParity; i++) {
        const uint8_t root = spec_power(spec_power(2, 11), 121 + i);
        for (int j = i + 1; j > 0; j--)
            generator[j] =
                static_cast<uint8_t>(generator[j - 1] ^ spec_multiply(generator[j], root));
        generator[0] = spec_multiply(generator[0], root);
    }
    uint8_t remainder[kParity] = {0};
    for (int i = 0; i < 223; i++) {
        const uint8_t feedback = data[i] ^ remainder[0];
        for (int j = 0; j < kParity - 1; j++)
            remainder[j] = static_cast<uint8_t>(
                remainder[j + 1] ^ spec_multiply(feedback, generator[kParity - 1 - j]));
        remainder[kParity - 1] = spec_multiply(feedback, generator[0]);
    }
    std::memcpy(parity, remainder, kParity);
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
    adsl::encode_traffic(packet, traffic);
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

// Issue 1 G.1.5 marks no fix with 0xFFFFFF, and reads 0x800000 as 90 S and 180 W.
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
        adsl::encode_traffic(packet, traffic);
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
    adsl::Packet other{};
    other.reset(adsl::Packet::kPayloadTypeOgnDiagnostics);
    CHECK(adsl::lowest_version(other) == 1);
}

// No signature promised, so the packet ends at its parity: 27 bytes of air, sync word included.
TEST_CASE("ADS-L.4.SRD860.E.1.2: no signature is promised, so nothing follows the parity") {
    adsl::Packet packet = traffic_packet();
    CHECK(((packet.version >> 4) & 0x01) == 0);
    CHECK(adsl::Packet::kTxBytes == 27);
    CHECK(adsl::Packet::kTxBytes == 3 + adsl::Packet::kDataBytes);
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
TEST_CASE("ADS-L.4.SRD860.E.2: the scrambler is XXTEA over the ADS-L data, six rounds") {
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

// INFO: fc 18sep26 prose: the layer's remit, and the note that issue 1 defined only the CRC variant
TEST_CASE("ADS-L.4.SRD860.E.3: error control is a CRC or an FEC, named by the header bit" *
          doctest::skip()) {
    FAIL("prose: the choice between E.3.1 and E.3.2, which the header's error control bit names");
}

// Four header fields in one byte, and this device sends a zero in every one of them.
// The Mode-S polynomial over the header and the scrambled data, most significant byte first.
TEST_CASE("ADS-L.4.SRD860.E.3.1: the parity is the 24-bit CRC the clause prints") {
    adsl::Packet packet = traffic_packet();
    packet.scramble();
    packet.set_crc();

    const uint32_t expected = spec_crc24(packet.data, adsl::Packet::kCrcCoverBytes);
    CHECK(packet.crc_bytes[0] == static_cast<uint8_t>(expected >> 16));
    CHECK(packet.crc_bytes[1] == static_cast<uint8_t>(expected >> 8));
    CHECK(packet.crc_bytes[2] == static_cast<uint8_t>(expected));
    CHECK(packet.check_crc() == 0);
    CHECK(adsl::Packet::kCrcCoverBytes == 21);
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
