// ADS-L 4 SRD-860 issue 2 Subpart D, the data link layer: packet framing, construction order, media
// access.
#include <cstdint>
#include <cstring>

#include "doctest/doctest.h"
#include "protocols/adsl/media_access.h"
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

// INFO: fc 13nov26 optional: G.2.3 lets a Status declare O-band LDR receive None, as ours does
TEST_CASE(
    "ADS-L.4.SRD860.D.1.2: the O-band LDR packet is a 6-byte header, 24 bytes of data and a "
    "CRC8" *
    doctest::skip()) {
    FAIL("optional: O-band LDR is not taken, so no header, no CRC8 and no 38.4 kbps dwell");
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

// The clause's two numbers, each read from the side a device could get wrong.
TEST_CASE("ADS-L.4.SRD860.D.3: a busy channel defers, forces after 3000 ms, then 2000 ms quiet") {
    adsl::ChannelAttempt attempt{};
    attempt.at_us = 20000000;
    attempt.first_us = attempt.at_us;
    attempt.earliest_us = attempt.at_us;
    attempt.last_us = attempt.at_us + 300000;
    attempt.may_force = true;
    const adsl::Deferral deferral{5000, 8, 3};

    attempt.clear = true;
    CHECK(adsl::media_access(attempt, deferral).access == adsl::Access::Transmit);
    attempt.clear = false;
    const adsl::AccessVerdict deferred = adsl::media_access(attempt, deferral);
    CHECK(deferred.access == adsl::Access::Defer);
    CHECK(deferred.at_us > attempt.at_us);

    attempt.first_us = attempt.at_us - 2999000;
    CHECK(adsl::media_access(attempt, deferral).access == adsl::Access::Defer);
    attempt.first_us = attempt.at_us - 3000000;
    const adsl::AccessVerdict forced = adsl::media_access(attempt, deferral);
    CHECK(forced.access == adsl::Access::Force);

    adsl::ChannelAttempt after = attempt;
    after.clear = true;
    after.forced_end_us = forced.at_us + 5000;
    after.at_us = forced.at_us + 5000 + 1999000;
    CHECK(adsl::media_access(after, deferral).access == adsl::Access::Silent);
    after.at_us = forced.at_us + 5000 + 2000000;
    CHECK(adsl::media_access(after, deferral).access == adsl::Access::Transmit);
}
