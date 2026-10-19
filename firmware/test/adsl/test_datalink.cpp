// ADS-L 4 SRD-860 issue 2 Subpart D, the data link layer: packet framing, construction order, media
// access.
#include <cstdint>
#include <cstring>

#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "core/timing/channel.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {
protocol::AdslPacket traffic_packet() {
    model::Ownship own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.climb_valid = true;
    own.lat_1e7 = 481234500;
    own.lon_1e7 = 81234500;
    own.alt_mm = 1234000;
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

}  // namespace

// INFO: fc 18sep26 prose: the three duties of the layer, each specified in the clauses below
TEST_CASE("ADS-L.4.SRD860.D.1: the data link mediates the channel, adds parity, may sign" *
          doctest::skip()) {
    FAIL("prose: the layer's remit, specified by D.1.1, D.1.2, D.3 and E.3");
}

// The length byte rides in the sync word, which is why a burst of any other length never frames.
TEST_CASE(
    "ADS-L.4.SRD860.D.1.1: the Packet Length field counts the message behind it, not itself") {
    protocol::AdslPacket p = traffic_packet();
    const int network_header = 1;
    const int adsl_data = 20;
    const int parity = 3;
    CHECK(int(p.Length) == network_header + adsl_data + parity);
    CHECK(int(p.Length) == protocol::AdslPacket::kDataBytes);
    CHECK(int(p.Length) == int(protocol::kAdslSyncWord & 0xFFu));
    CHECK(protocol::kUplinkSync[2] == protocol::kUplinkFrameBytes);
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
    protocol::AdslPacket p = traffic_packet();
    const protocol::AdslPacket plain = p;
    p.scramble();
    p.set_crc();
    CHECK(std::memcmp(p.Byte, plain.Byte, 20) != 0);
    CHECK(p.check_crc() == 0);

    protocol::AdslPacket flipped = p;
    flipped.Version ^= 0x01;
    CHECK(flipped.check_crc() != 0);

    protocol::AdslPacket descrambled = p;
    descrambled.descramble();
    CHECK(descrambled.check_crc() != 0);
    CHECK(std::memcmp(descrambled.Byte, plain.Byte, 20) == 0);
}

// TODO: fc 18sep26 no listen-before-talk: the hour's duty cycle is the only rule refusing a burst
TEST_CASE("ADS-L.4.SRD860.D.3: a burst waits on a carrier sense, and forces after 3000 ms" *
          doctest::skip()) {
    FAIL("no listen-before-talk, so neither the 3000 ms force nor the 2000 ms silence after it");
}

// The route taken instead of polite spectrum access, and the only rule that does refuse a burst.
TEST_CASE("ADS-L.4.SRD860.D.3: nothing refuses a burst here but the hour's duty cycle") {
    timing::AirTime air;
    CHECK(timing::AirTime::kLimitPermille == 10u);
    const uint32_t budget_ms = timing::AirTime::kBudgetMs;
    air.spend(0, budget_ms);
    CHECK_FALSE(air.may_spend(0, 1));
    CHECK(air.may_spend(timing::AirTime::kWindowMs + 1, 1));
}
