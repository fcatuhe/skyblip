// ADS-L 4 SRD-860 issue 2 Subpart D, the data link layer: packet framing, construction order, media
// access.
#include <cstdint>
#include <cstring>

#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/physical.h"
#include "protocols/adsl/test/conformance/reference.h"
#include "protocols/adsl/uplink.h"

using namespace skyblip;
using namespace skyblip::adsl::conformance;

// INFO: fc 18sep26 prose: the three duties of the layer, each specified in the clauses below
TEST_CASE("ADS-L.4.SRD860.D.1: the data link mediates the channel, adds parity, may sign" *
          doctest::skip()) {
    FAIL("prose: the layer's remit, specified by D.1.1, D.1.2, D.3 and E.3");
}

// The M-band length byte rides in the sync word, which is why a burst of any other length never
// frames.
TEST_CASE(
    "ADS-L.4.SRD860.D.1.1: the Packet Length field counts the message behind it, not itself") {
    const int network_header = 1;
    const int adsl_data = 20;
    const int parity = 3;
    CHECK(int(adsl::Packet::kDataBytes) == network_header + adsl_data + parity);
    CHECK(sizeof(adsl::Packet) == adsl::Packet::kDataBytes);
    CHECK(int(adsl::kSyncWord & 0xFFu) == adsl::Packet::kDataBytes);
    const int uplink_header = 5;
    const int fec_parity = 32;
    CHECK(adsl::uplink_length(1) == network_header + uplink_header + 19 + fec_parity);
    CHECK(adsl::uplink_length(10) == network_header + uplink_header + 191 + fec_parity);
}

// TODO: fc 18sep26 no O-band LDR radio here: the dwell map carries M-band and O-band HDR only
TEST_CASE(
    "ADS-L.4.SRD860.D.1.2: the O-band LDR packet is a 6-byte header, 24 bytes of data and a "
    "CRC8" *
    doctest::skip()) {
    FAIL("O-band LDR is not implemented: no header, no CRC8, no 38.4 kbps dwell");
}

// The parity covers the scrambled form, so a receiver checks it before it can read a single field.
TEST_CASE("ADS-L.4.SRD860.D.2: data, then scrambling, then parity over the network header too") {
    adsl::Packet packet = traffic_packet();
    const adsl::Packet plain = packet;
    packet.scramble();
    packet.set_crc();
    CHECK(std::memcmp(packet.payload, plain.payload, 20) != 0);
    CHECK(packet.check_crc() == 0);

    adsl::Packet flipped = packet;
    flipped.version ^= 0x01;
    CHECK(flipped.check_crc() != 0);

    adsl::Packet descrambled = packet;
    descrambled.descramble();
    CHECK(descrambled.check_crc() != 0);
    CHECK(std::memcmp(descrambled.payload, plain.payload, 20) == 0);
}

// TODO: fc 18sep26 no listen-before-talk: the hour's duty cycle is the only rule refusing a burst
TEST_CASE("ADS-L.4.SRD860.D.3: a burst waits on a carrier sense, and forces after 3000 ms" *
          doctest::skip()) {
    FAIL("no listen-before-talk, so neither the 3000 ms force nor the 2000 ms silence after it");
}
