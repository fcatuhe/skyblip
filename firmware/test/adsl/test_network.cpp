// ADS-L 4 SRD-860 issue 2 Subpart E, the network layer: header, scrambler, parity and signature.
#include <cstdint>
#include <cstring>

#include "core/fec/reed_solomon.h"
#include "core/fec/scramble.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/air.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {
protocol::AdslPacket traffic_packet() {
    model::Ownship own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.climb_valid = true;
    own.latitude_1e7 = 481234500;
    own.longitude_1e7 = 81234500;
    own.altitude_hae_mm = 1234000;
    own.speed_mm_s = 45000;  // 180 quarter-m/s on the wire
    own.climb_mm_s = -5500;  // -44 eighths on the wire
    own.track_cdeg = 18000;
    own.hdop_e2 = 90;
    own.vdop_e2 = 150;
    own.flight_state = 2;
    protocol::AdslPacket p{};
    protocol::from_own(p, own, 0x123456, 6, 4);
    return p;
}

// E.2's own sample implementation, transcribed, with the zero key the clause names for scrambling.
void spec_xxtea(uint32_t* data, uint32_t words, const uint32_t* key, uint32_t rounds) {
    const uint32_t kDelta = 0x9E3779B9u;
    uint32_t y = data[0];
    uint32_t sum = 0;
    uint32_t z = data[words - 1];
    uint32_t q = rounds;
    while (q-- > 0) {
        sum += kDelta;
        const uint32_t e = (sum >> 2) & 3u;
        uint32_t p = 0;
        for (p = 0; p < words - 1; p++) {
            y = data[p + 1];
            data[p] += (((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4))) ^
                       ((sum ^ y) + (key[(p & 3u) ^ e] ^ z));
            z = data[p];
        }
        y = data[0];
        data[words - 1] +=
            (((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4))) ^ ((sum ^ y) + (key[(p & 3u) ^ e] ^ z));
        z = data[words - 1];
    }
}

// E.3.1's own sample implementation, transcribed.
uint32_t spec_crc24(const uint8_t* data, size_t length) {
    const uint32_t kPoly = 0xFFFA0480u;
    uint32_t crc = 0;
    for (size_t i = 0; i < length + 3; i++) {
        crc |= i < length ? data[i] : 0u;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80000000u) crc ^= kPoly;
            crc <<= 1;
        }
    }
    return crc >> 8;
}
}  // namespace

TEST_CASE("ADS-L.4.SRD860.E.1: the network header is one byte, and this one claims nothing") {
    protocol::AdslPacket p = traffic_packet();
    const uint8_t version = p.version & 0x0F;
    const uint8_t signed_flag = (p.version >> 4) & 0x01;
    const uint8_t key_index = (p.version >> 5) & 0x03;
    const uint8_t error_control = (p.version >> 7) & 0x01;
    CHECK(version == 0);
    CHECK(signed_flag == 0);
    CHECK(key_index == 0);
    CHECK(error_control == 0);
    CHECK(p.version == 0x00);
}

// Issue 2 is version 1, but this payload encodes as Issue 1's did, and the clause wants the lowest.
TEST_CASE("ADS-L.4.SRD860.E.1.1: version 0 goes out, and a version 1 payload reads the same way") {
    protocol::AdslPacket p = traffic_packet();
    CHECK((p.version & 0x0F) == 0);

    model::AircraftObservation as_sent{};
    REQUIRE(protocol::to_observation(p, events::Stamp{}, -80, model::Source::AdslDirect, as_sent));

    protocol::AdslPacket issue2 = p;
    issue2.version = (issue2.version & 0xF0) | 0x01;
    model::AircraftObservation as_received{};
    REQUIRE(protocol::to_observation(issue2, events::Stamp{}, -80, model::Source::AdslDirect,
                                     as_received));
    CHECK(as_received.address == as_sent.address);
    CHECK(as_received.latitude_1e7 == as_sent.latitude_1e7);
    CHECK(as_received.altitude_m == as_sent.altitude_m);
}

// No signature promised, so the packet ends at its parity: 27 bytes of air, sync word included.
TEST_CASE("ADS-L.4.SRD860.E.1.2: no signature is promised, so nothing follows the parity") {
    protocol::AdslPacket p = traffic_packet();
    CHECK(((p.version >> 4) & 0x01) == 0);
    CHECK(protocol::AdslPacket::kTxBytes == 27);
    CHECK(protocol::AdslPacket::kTxBytes == 3 + protocol::AdslPacket::kDataBytes);
}

TEST_CASE("ADS-L.4.SRD860.E.1.3: key 0 is the scrambling key, and it is the key used") {
    protocol::AdslPacket p = traffic_packet();
    CHECK(((p.version >> 5) & 0x03) == 0);

    uint32_t words[5] = {0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u, 0x12345678u};
    uint32_t mirror[5];
    std::memcpy(mirror, words, sizeof(words));
    const uint32_t zero_key[4] = {0, 0, 0, 0};
    fec::xxtea_scramble_key0(words, 5, 6);
    spec_xxtea(mirror, 5, zero_key, 6);
    for (int i = 0; i < 5; i++) CHECK(words[i] == mirror[i]);
}

// E.2's table prints num_data_words 6, F.1 makes a Traffic packet 5 words, and 5 is what is
// scrambled.
TEST_CASE("ADS-L.4.SRD860.E.2: the scrambler is XXTEA over the ADS-L data, six rounds") {
    protocol::AdslPacket p = traffic_packet();
    uint32_t mirror[5];
    std::memcpy(mirror, p.payload, sizeof(mirror));
    const uint32_t zero_key[4] = {0, 0, 0, 0};
    spec_xxtea(mirror, 5, zero_key, 6);

    p.scramble();
    CHECK(std::memcmp(p.payload, mirror, sizeof(mirror)) == 0);
    CHECK(sizeof(mirror) % 4 == 0);

    p.descramble();
    protocol::AdslPacket again = traffic_packet();
    CHECK(std::memcmp(p.payload, again.payload, 20) == 0);
}

// INFO: fc 18sep26 prose: the layer's remit, and the note that issue 1 defined only the CRC variant
TEST_CASE("ADS-L.4.SRD860.E.3: error control is a CRC or an FEC, named by the header bit" *
          doctest::skip()) {
    FAIL("prose: the choice between E.3.1 and E.3.2, which the header's error control bit names");
}

// Four header fields in one byte, and this device sends a zero in every one of them.
// The Mode-S polynomial over the header and the scrambled data, most significant byte first.
TEST_CASE("ADS-L.4.SRD860.E.3.1: the parity is the 24-bit CRC the clause prints") {
    protocol::AdslPacket p = traffic_packet();
    p.scramble();
    p.set_crc();

    const uint32_t expected = spec_crc24(p.data, protocol::AdslPacket::kCrcCoverBytes);
    CHECK(p.crc_bytes[0] == static_cast<uint8_t>(expected >> 16));
    CHECK(p.crc_bytes[1] == static_cast<uint8_t>(expected >> 8));
    CHECK(p.crc_bytes[2] == static_cast<uint8_t>(expected));
    CHECK(p.check_crc() == 0);
    CHECK(protocol::AdslPacket::kCrcCoverBytes == 21);
}

TEST_CASE("ADS-L.4.SRD860.E.3.2: the forward error correction is Reed-Solomon over 255 bytes") {
    CHECK(fec::ReedSolomon255::kCodewordBytes == 255);
    CHECK(fec::ReedSolomon255::kDataBytes == 223);
    CHECK(fec::ReedSolomon255::kParityBytes == 32);
    CHECK(fec::ReedSolomon255::kMaxErrors == 16);
}

// TODO: fc 18sep26 the roots here are alpha^0 up, the clause wants element 11 from root 121
TEST_CASE("ADS-L.4.SRD860.E.3.2: the code is generated from the roots the clause names" *
          doctest::skip()) {
    FAIL(
        "neither the roots nor the zero-prefixed 223-byte block are the clause's, so a ground "
        "station's frame would not decode");
}

// Optional, and not taken: no key is held, so no packet is signed and none claims to be.
TEST_CASE("ADS-L.4.SRD860.E.4: nothing is signed, and nothing says it is") {
    protocol::AdslPacket p = traffic_packet();
    p.scramble();
    p.set_crc();
    CHECK(((p.version >> 4) & 0x01) == 0);
    CHECK(protocol::kAdslFrameBytes == protocol::AdslPacket::kDataBytes);

    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t length =
        protocol::encode_mband(protocol::kAdslSyncWord, p.data, protocol::kAdslFrameBytes, chips);
    CHECK(length == 2u * (protocol::kSyncWordBytes + protocol::kAdslFrameBytes));
}

// TODO: fc 18sep26 no signature, so no UNIX timestamp is appended to one either
TEST_CASE("ADS-L.4.SRD860.E.4.1: a signature carries the UTC second it was made in" *
          doctest::skip()) {
    FAIL("no secure signature is produced, so there is no timestamp field to fill");
}

// TODO: fc 18sep26 no Ed25519 key is held, generated or stored on this device
TEST_CASE("ADS-L.4.SRD860.E.4.2: the signature is Ed25519 over the header, the data and the time" *
          doctest::skip()) {
    FAIL("no Ed25519 key is held, so nothing signs a packet");
}
