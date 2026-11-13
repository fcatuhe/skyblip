// ADS-L 4 SRD-860 issue 2 Subpart B, the overview: bit order, transmission sequence and versioning.
#include <cstdint>

#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/physical.h"
#include "protocols/adsl/test/conformance/reference.h"
#include "protocols/common/little_endian.h"
#include "protocols/common/manchester.h"

using namespace skyblip;
using namespace skyblip::adsl::conformance;

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
    wire::put_uint24(three, 0x14471Au);
    CHECK(three[0] == 0x1A);
    CHECK(three[1] == 0x47);
    CHECK(three[2] == 0x14);
    CHECK(wire::get_uint24(three) == 0x14471Au);

    uint8_t four[4] = {0, 0, 0, 0};
    wire::put_uint32(four, 0x89ABCDEFu);
    CHECK(four[0] == 0xEF);
    CHECK(four[3] == 0x89);
    CHECK(wire::get_uint32(four) == 0x89ABCDEFu);
}

// Example 2's packing rule, read off the Meta block G.1 puts at payload offset 0.
TEST_CASE("ADS-L.4.SRD860.B.3: a field starts at the bit its offset names, counting from bit 0") {
    adsl::Packet packet{};
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
    CHECK(chips_of(0x80) == wire::manchester_chips(0x80));
}

// The preamble is the radio's: what the firmware hands it runs from the sync word to the last byte.
TEST_CASE("ADS-L.4.SRD860.B.4: a transmission is the sync word and the packet, nothing behind it") {
    adsl::Packet packet = traffic_packet();
    packet.scramble();
    packet.set_crc();

    uint8_t chips[kTxChipBytes] = {0};
    const size_t chip_bytes =
        wire::manchester_frame(adsl::kSyncWord, packet.data, adsl::kFrameBytes, chips);
    CHECK(chip_bytes == 2u * (wire::kSyncWordBytes + adsl::kFrameBytes));

    uint8_t decoded[wire::kSyncWordBytes + adsl::kFrameBytes] = {0};
    uint8_t error[sizeof(decoded)] = {0};
    wire::manchester_decode(chips, sizeof(decoded), decoded, error);
    for (uint8_t error_bits : error) CHECK(error_bits == 0);
    CHECK(decoded[1] == 0x72);
    CHECK(decoded[2] == 0x4B);
    CHECK(decoded[3] == adsl::kFrameBytes);
    for (int i = 0; i < adsl::kFrameBytes; i++)
        CHECK(decoded[wire::kSyncWordBytes + i] == packet.data[i]);
}

// Both reserved bits set: the one at header offset 38, and the one closing the payload.
TEST_CASE("ADS-L.4.SRD860.B.6: a reserved bit a sender filled changes nothing a receiver reads") {
    adsl::Packet clean = traffic_packet();
    adsl::Packet noisy = clean;
    noisy.address_bytes[3] |= 0x40;
    noisy.reserved = 1;

    adsl::Traffic from_clean;
    adsl::Traffic from_noisy;
    REQUIRE(heard(clean, from_clean));
    REQUIRE(heard(noisy, from_noisy));

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
