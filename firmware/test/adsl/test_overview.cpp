// ADS-L 4 SRD-860 issue 2 Subpart B, the overview: bit order, transmission sequence and versioning.
#include <cstdint>

#include "core/fec/manchester.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/air.h"
#include "core/util/little_endian.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {
uint16_t chips_of(uint8_t byte) {
    uint8_t coded[2] = {0, 0};
    fec::manchester_encode(&byte, 1, coded);
    return static_cast<uint16_t>((coded[0] << 8) | coded[1]);
}

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
    protocol::AdslPacket packet{};
    protocol::from_own(packet, own, 0x123456, 6, 4);
    return packet;
}
}  // namespace

// INFO: fc 18sep26 prose: a GNSS receiver, a host processor and an RF front end, so this board
TEST_CASE("ADS-L.4.SRD860.B.1: an implementation is GNSS, a host processor and an RF front end" *
          doctest::skip()) {
    FAIL("prose: a block diagram, with nothing a device executes");
}

// INFO: fc 18sep26 prose: ISO/IEC 7498-1 layers 1 to 3 and 6 to 7, which are Subparts C to G
TEST_CASE("ADS-L.4.SRD860.B.2: ADS-L is a stateless broadcast protocol over five OSI layers" *
          doctest::skip()) {
    FAIL("prose: an architecture table, with nothing a device executes");
}

// The clause's own Example 1: 1328922 (0x14471A) leaves as 0x1A, 0x47, 0x14.
TEST_CASE("ADS-L.4.SRD860.B.3: an integer rides the air least significant byte first") {
    uint8_t three[3] = {0, 0, 0};
    put_u24(three, 0x14471Au);
    CHECK(three[0] == 0x1A);
    CHECK(three[1] == 0x47);
    CHECK(three[2] == 0x14);
    CHECK(get_u24(three) == 0x14471Au);

    uint8_t four[4] = {0, 0, 0, 0};
    put_u32(four, 0x89ABCDEFu);
    CHECK(four[0] == 0xEF);
    CHECK(four[3] == 0x89);
    CHECK(get_u32(four) == 0x89ABCDEFu);
}

// Example 2's packing rule, read off the Meta block G.1 puts at payload offset 0.
TEST_CASE("ADS-L.4.SRD860.B.3: a field starts at the bit its offset names, counting from bit 0") {
    protocol::AdslPacket packet{};
    packet.reset();
    packet.timestamp = 0x2A;
    packet.flight_state = 2;
    packet.aircraft_category = 0x15;
    packet.emergency = 5;
    const uint8_t* meta = &packet.address_bytes[4];
    CHECK(meta[0] == static_cast<uint8_t>(0x2A | (2u << 6)));
    CHECK(meta[1] == static_cast<uint8_t>(0x15 | (5u << 5)));
}

TEST_CASE("ADS-L.4.SRD860.B.3: a byte goes out most significant bit first") {
    CHECK(chips_of(0x80) == 0x6AAA);
    CHECK(chips_of(0x01) == 0xAAA9);
    CHECK(chips_of(0x80) == protocol::manchester_chips(0x80));
}

// The preamble is the radio's: what the firmware hands it runs from the sync word to the last byte.
TEST_CASE("ADS-L.4.SRD860.B.4: a transmission is the sync word and the packet, nothing behind it") {
    protocol::AdslPacket packet = traffic_packet();
    packet.scramble();
    packet.set_crc();

    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_bytes = protocol::encode_m_band(protocol::kAdslSyncWord, packet.data,
                                                      protocol::kAdslFrameBytes, chips);
    CHECK(chip_bytes == 2u * (protocol::kSyncWordBytes + protocol::kAdslFrameBytes));

    uint8_t decoded[protocol::kSyncWordBytes + protocol::kAdslFrameBytes] = {0};
    uint8_t error[sizeof(decoded)] = {0};
    fec::manchester_decode(chips, sizeof(decoded), decoded, error);
    for (uint8_t error_bits : error) CHECK(error_bits == 0);
    CHECK(decoded[1] == 0x72);
    CHECK(decoded[2] == 0x4B);
    CHECK(decoded[3] == protocol::kAdslFrameBytes);
    for (int i = 0; i < protocol::kAdslFrameBytes; i++)
        CHECK(decoded[protocol::kSyncWordBytes + i] == packet.data[i]);
}

// TODO: fc 18sep26 soft versioning needs G.2's Max Protocol Version, neither sent nor tracked
TEST_CASE("ADS-L.4.SRD860.B.5: the version transmitted is one every close neighbour can read" *
          doctest::skip()) {
    FAIL(
        "no peer's Max Protocol Version is tracked, so nothing lowers the version for a neighbour");
}

// Both reserved bits set: the one at header offset 38, and the one closing the payload.
TEST_CASE("ADS-L.4.SRD860.B.6: a reserved bit a sender filled changes nothing a receiver reads") {
    protocol::AdslPacket clean = traffic_packet();
    protocol::AdslPacket noisy = clean;
    noisy.address_bytes[3] |= 0x40;
    noisy.reserved = 1;

    model::AircraftObservation from_clean{};
    model::AircraftObservation from_noisy{};
    REQUIRE(protocol::to_observation(clean, events::Stamp{}, -80, model::Source::AdslDirect,
                                     from_clean));
    REQUIRE(protocol::to_observation(noisy, events::Stamp{}, -80, model::Source::AdslDirect,
                                     from_noisy));

    CHECK(from_noisy.address == from_clean.address);
    CHECK(from_noisy.address_mapping_table == from_clean.address_mapping_table);
    CHECK(from_noisy.latitude_1e7 == from_clean.latitude_1e7);
    CHECK(from_noisy.longitude_1e7 == from_clean.longitude_1e7);
    CHECK(from_noisy.altitude_m == from_clean.altitude_m);
    CHECK(from_noisy.speed_q == from_clean.speed_q);
    CHECK(from_noisy.climb_e8 == from_clean.climb_e8);
    CHECK(from_noisy.track_c9 == from_clean.track_c9);
    CHECK(from_noisy.aircraft_category == from_clean.aircraft_category);
}
