// ADS-L 4 SRD-860 issue 2 Subpart C, the physical layer: bands, modulation, coding and sync words.
#include <cstdint>

#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/physical.h"
#include "protocols/adsl/test/conformance/reference.h"
#include "protocols/adsl/uplink.h"
#include "protocols/common/manchester.h"

using namespace skyblip;
using namespace skyblip::adsl::conformance;

// INFO: fc 18sep26 prose, but its two bands are the tables below, and those are checked
TEST_CASE("ADS-L.4.SRD860.C.1: ADS-L runs on an M band of two channels and an O band" *
          doctest::skip()) {
    FAIL("prose: an introduction to the bands, whose parameters are C.2, C.3 and C.4");
}

TEST_CASE("ADS-L.4.SRD860.C.2: the M band is modulated as the table prints it") {
    CHECK(adsl::kMBand0Hz == 868200000u);
    CHECK(adsl::kMBand1Hz == 868400000u);
    CHECK(adsl::kMBandChannelBandwidthHz == 200000u);
    CHECK(adsl::kMBandChipRateBps == 100000u);
    CHECK(adsl::kMBandDeviationHz == 50000u);
    CHECK(adsl::kMBandGaussianBtE2 == 50u);
    CHECK(adsl::kMBandErpCeilingDbm == 14);
}

TEST_CASE("ADS-L.4.SRD860.C.2.1: a one is sent as 01 and a zero as 10") {
    CHECK(chips_of(0xFF) == 0x5555);
    CHECK(chips_of(0x00) == 0xAAAA);
    CHECK(chips_of(0x72) == 0x95A6);

    uint8_t frame[adsl::kFrameBytes] = {0};
    for (int i = 0; i < adsl::kFrameBytes; i++) frame[i] = static_cast<uint8_t>(i * 7 + 3);
    uint8_t chips[2 * adsl::kFrameBytes] = {0};
    wire::manchester_encode(frame, sizeof(frame), chips);
    uint8_t back[sizeof(frame)] = {0};
    uint8_t error[sizeof(frame)] = {0};
    wire::manchester_decode(chips, sizeof(frame), back, error);
    for (size_t i = 0; i < sizeof(frame); i++) {
        CHECK(back[i] == frame[i]);
        CHECK(error[i] == 0);
    }
}

TEST_CASE("ADS-L.4.SRD860.C.2.4: the sync word is 0x72 0x4B, Manchester-coded") {
    CHECK(static_cast<uint8_t>(adsl::kSyncWord >> 16) == 0x72);
    CHECK(static_cast<uint8_t>(adsl::kSyncWord >> 8) == 0x4B);

    uint8_t chips[kTxChipBytes] = {0};
    uint8_t frame[adsl::kFrameBytes] = {0};
    wire::manchester_frame(adsl::kSyncWord, frame, sizeof(frame), chips);
    CHECK(chips[2] == static_cast<uint8_t>(chips_of(0x72) >> 8));
    CHECK(chips[3] == static_cast<uint8_t>(chips_of(0x72)));
    CHECK(chips[4] == static_cast<uint8_t>(chips_of(0x4B) >> 8));
    CHECK(chips[5] == static_cast<uint8_t>(chips_of(0x4B)));
}

// INFO: fc 13nov26 optional: G.2.3 lets a Status declare O-band LDR receive None, as ours does
TEST_CASE("ADS-L.4.SRD860.C.3: the O-band LDR channel is 38.4 kbps behind sync word 0xB4 0x2B" *
          doctest::skip()) {
    FAIL("optional: O-band LDR is not taken, so nothing tunes 869.525 MHz at 38.4 kbps");
}

// INFO: fc 13nov26 optional: G.2.3 lets a Status declare O-band LDR receive None, as ours does
TEST_CASE("ADS-L.4.SRD860.C.3.1: the O-band LDR sync word is 0xB4 0x2B" * doctest::skip()) {
    FAIL("optional: O-band LDR is not taken, so no sync word is armed for that channel");
}

// INFO: fc 13nov26 optional: G.2.3 lets a Status declare O-band LDR receive None, as ours does
TEST_CASE("ADS-L.4.SRD860.C.3.2: the O-band LDR preamble is 10, forty times" * doctest::skip()) {
    FAIL("optional: O-band LDR is not taken, so no preamble is emitted on that channel");
}

TEST_CASE("ADS-L.4.SRD860.C.4: the O-band HDR channel is modulated as the table prints it") {
    CHECK(adsl::kOBandHz == 869525000u);
    CHECK(adsl::kUplinkChannelBandwidthHz == 250000u);
    CHECK(adsl::kUplinkChipRateBps == 200000u);
    CHECK(adsl::kUplinkDeviationHz == 50000u);
    CHECK(adsl::kUplinkGaussianBtE2 == 50u);
    // GMSK is GFSK at modulation index 0.5, which is what the table's implicit deviation says.
    CHECK(4u * adsl::kUplinkDeviationHz == adsl::kUplinkChipRateBps);
}

TEST_CASE("ADS-L.4.SRD860.C.4.2: the O-band HDR preamble is 10, twelve times") {
    CHECK(adsl::kUplinkPreambleChips == 24u);
    CHECK(adsl::kUplinkPreambleChips / 2u == 12u);
}

TEST_CASE("ADS-L.4.SRD860.C.4.3: the O-band HDR sync word is 0x2D 0xD4, uncoded") {
    CHECK(adsl::kUplinkSync[0] == 0x2D);
    CHECK(adsl::kUplinkSync[1] == 0xD4);

    uint8_t packet[1 + adsl::kUplinkMaxLength] = {0};
    packet[0] = static_cast<uint8_t>(adsl::uplink_length(1));
    packet[1] = 0xA5;
    uint8_t out[adsl::kUplinkMaxBurstBytes] = {0};
    CHECK(adsl::encode_o_band(packet, out) == 3u + adsl::uplink_length(1));
    CHECK(out[0] == 0x2D);
    CHECK(out[1] == 0xD4);
    CHECK(out[2] == adsl::uplink_length(1));
    CHECK(out[3] == 0xA5);
}
