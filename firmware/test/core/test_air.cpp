// The shared sync window: one M-band dwell, both systems. If any of these fail,
// the receiver is deaf to half the sky and does not know it.
#include <cstdint>
#include <cstring>

#include "core/fec/manchester.h"
#include "core/model/band.h"
#include "core/protocol/air.h"
#include "doctest/doctest.h"
#include "test/support/rf_channel.h"

using namespace skyblip;

namespace {

// A burst as it leaves a transmitter, then as the radio reports it: the matched window is gone.
void deliver(const uint8_t* chips, size_t chip_length, uint8_t* out, size_t out_length) {
    const size_t first_bit = protocol::kSharedSyncEndChip;
    for (size_t i = 0; i < out_length; i++) {
        uint8_t byte = 0;
        for (int bit_in_byte = 0; bit_in_byte < 8; bit_in_byte++) {
            const size_t bit = first_bit + i * 8 + static_cast<size_t>(bit_in_byte);
            const bool set = bit < chip_length * 8 && ((chips[bit >> 3] >> (7 - (bit & 7))) & 1u);
            byte = static_cast<uint8_t>((byte << 1) | (set ? 1u : 0u));
        }
        out[i] = byte;
    }
}

void frame_of(uint32_t sync_word, const uint8_t* payload, uint8_t payload_length,
              protocol::Frame& out) {
    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_length = protocol::encode_m_band(sync_word, payload, payload_length, chips);
    uint8_t reported[protocol::kRxChipBytes] = {0};
    deliver(chips, chip_length, reported, sizeof(reported));
    REQUIRE(protocol::receive_m_band(reported, sizeof(reported), out));
}

void fill(uint8_t* bytes, uint8_t length, uint8_t seed) {
    for (uint8_t i = 0; i < length; i++) bytes[i] = static_cast<uint8_t>(i * 31 + seed);
}

}  // namespace

TEST_CASE("air: the constexpr chip coding is the one fec::manchester_encode uses") {
    for (int byte = 0; byte < 256; byte++) {
        uint8_t coded[2];
        const uint8_t in = static_cast<uint8_t>(byte);
        fec::manchester_encode(&in, 1, coded);
        const uint16_t chips = protocol::manchester_chips(in);
        CHECK(coded[0] == static_cast<uint8_t>(chips >> 8));
        CHECK(coded[1] == static_cast<uint8_t>(chips));
    }
}

// The trick: two sync words whose chips agree for 18, and a detector armed with 16 of them.
TEST_CASE("air: the two sync words share the window the detector matches") {
    CHECK(protocol::kAdslSyncWord != protocol::kAlptasSyncWord);
    CHECK(protocol::shared_sync(protocol::kAdslSyncWord) ==
          protocol::shared_sync(protocol::kAlptasSyncWord));
    // The chips of 0xF5, the sync-word byte both systems open on.
    CHECK(protocol::kSharedSync[0] == 0x55);
    CHECK(protocol::kSharedSync[1] == 0x99);

    // The 24 bits of sync word still ahead of the frame when the report starts name the system.
    CHECK(protocol::kSyncTailBits == 24);
    CHECK(protocol::sync_tail(protocol::kAdslSyncWord) == 0x724B18u);
    CHECK(protocol::sync_tail(protocol::kAlptasSyncWord) == 0x31FAB6u);
}

TEST_CASE("air: an ADS-L burst frames as ADS-L, byte-aligned on its data") {
    uint8_t payload[protocol::kAdslFrameBytes];
    fill(payload, sizeof(payload), 7);

    protocol::Frame frame{};
    frame_of(protocol::kAdslSyncWord, payload, sizeof(payload), frame);
    CHECK(frame.system == protocol::System::AdslDirect);
    CHECK(frame.bytes == protocol::kAdslFrameBytes);
    CHECK(std::memcmp(frame.data, payload, sizeof(payload)) == 0);
    CHECK(frame.bad_chips == 0);
}

TEST_CASE("air: an ALP-TAS burst frames as ALP-TAS through the same window") {
    uint8_t payload[protocol::kAlptasFrameBytes];
    fill(payload, sizeof(payload), 19);

    protocol::Frame frame{};
    frame_of(protocol::kAlptasSyncWord, payload, sizeof(payload), frame);
    CHECK(frame.system == protocol::System::Alptas);
    CHECK(frame.bytes == protocol::kAlptasFrameBytes);
    CHECK(std::memcmp(frame.data, payload, sizeof(payload)) == 0);
}

// The burst two devices on a bench could not exchange: both had written the window into the buffer.
TEST_CASE("air: the buffer a radio is handed is the burst, less the window the radio inserts") {
    uint8_t payload[protocol::kAdslFrameBytes];
    fill(payload, sizeof(payload), 23);

    uint8_t on_air[protocol::kTxChipBytes] = {0};
    const size_t air_length =
        protocol::encode_m_band(protocol::kAdslSyncWord, payload, sizeof(payload), on_air);
    uint8_t buffer[protocol::kTxPayloadChipBytes] = {0};
    const size_t buffer_length =
        protocol::m_band_payload(protocol::kAdslSyncWord, payload, sizeof(payload), buffer);

    CHECK(air_length == buffer_length + protocol::kSyncWindowChipBytes);
    CHECK(std::memcmp(on_air, protocol::kSharedSync, protocol::kSyncWindowChipBytes) == 0);
    CHECK(std::memcmp(on_air + protocol::kSyncWindowChipBytes, buffer, buffer_length) == 0);
}

// A dead chip pair in the sync tail must not cost the frame: it is 24 bits long
// and the two systems differ in far more than one of them.
TEST_CASE("air: one flipped chip in the sync tail still names the system") {
    uint8_t payload[protocol::kAdslFrameBytes];
    fill(payload, sizeof(payload), 3);
    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_length =
        protocol::encode_m_band(protocol::kAdslSyncWord, payload, sizeof(payload), chips);
    chips[5] ^= 0x40;  // inside the sync word, past the matched window

    uint8_t reported[protocol::kRxChipBytes] = {0};
    deliver(chips, chip_length, reported, sizeof(reported));
    protocol::Frame frame{};
    REQUIRE(protocol::receive_m_band(reported, sizeof(reported), frame));
    CHECK(frame.system == protocol::System::AdslDirect);
    CHECK(std::memcmp(frame.data, payload, sizeof(payload)) == 0);
}

TEST_CASE("air: a chip error inside the frame is reported, not hidden") {
    uint8_t payload[protocol::kAdslFrameBytes];
    fill(payload, sizeof(payload), 11);
    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_length =
        protocol::encode_m_band(protocol::kAdslSyncWord, payload, sizeof(payload), chips);
    chips[20] ^= 0x01;  // one chip of a pair: no transition left, so no bit either

    uint8_t reported[protocol::kRxChipBytes] = {0};
    deliver(chips, chip_length, reported, sizeof(reported));
    protocol::Frame frame{};
    REQUIRE(protocol::receive_m_band(reported, sizeof(reported), frame));
    CHECK(frame.system == protocol::System::AdslDirect);
    CHECK(frame.bad_chips > 0);
    bool flagged = false;
    for (uint8_t i = 0; i < frame.bytes; i++)
        if (frame.erasures[i] != 0) flagged = true;
    CHECK(flagged);
}

TEST_CASE("air: a foreign sync word frames as nothing") {
    uint8_t payload[protocol::kAdslFrameBytes];
    fill(payload, sizeof(payload), 5);
    uint8_t chips[protocol::kTxChipBytes] = {0};
    // Same leading byte, so the shared window still matches, then a tail neither
    // system uses: OGN's own sync word past its first byte.
    const size_t chip_length =
        protocol::encode_m_band(0xF5F3656Cu, payload, sizeof(payload), chips);

    uint8_t reported[protocol::kRxChipBytes] = {0};
    deliver(chips, chip_length, reported, sizeof(reported));
    protocol::Frame frame{};
    CHECK_FALSE(protocol::receive_m_band(reported, sizeof(reported), frame));
    CHECK(frame.system == protocol::System::Unknown);
    CHECK(frame.bytes == 0);
    CHECK_FALSE(protocol::framed_noise(frame));
}

// The 16-chip window is thin enough that the band's own noise walks through it.
TEST_CASE("air: the window a neighbour missed reads apart from the one noise walked through") {
    uint8_t reported[protocol::kRxChipBytes];
    RfChannel channel(0xA1B2);
    for (uint8_t& byte : reported) byte = static_cast<uint8_t>(channel.next());

    protocol::Frame frame{};
    CHECK_FALSE(protocol::receive_m_band(reported, sizeof(reported), frame));
    CHECK(frame.system == protocol::System::Unknown);
    CHECK(protocol::framed_noise(frame));
}

TEST_CASE("air: a foreign burst keeps its chips, so it is not counted as noise") {
    uint8_t payload[protocol::kAdslFrameBytes];
    fill(payload, sizeof(payload), 5);
    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_length =
        protocol::encode_m_band(0xF5F3656Cu, payload, sizeof(payload), chips);

    uint8_t reported[protocol::kRxChipBytes] = {0};
    deliver(chips, chip_length, reported, sizeof(reported));
    protocol::Frame frame{};
    CHECK_FALSE(protocol::receive_m_band(reported, sizeof(reported), frame));
    CHECK_FALSE(protocol::framed_noise(frame));
}

// The dwell reads past the end of a short burst, and those chips are nobody's.
TEST_CASE("air: the noise window ends inside the shortest burst the band carries") {
    uint8_t payload[protocol::kAdslFrameBytes];
    fill(payload, sizeof(payload), 5);
    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_length =
        protocol::encode_m_band(0xF5F3656Cu, payload, sizeof(payload), chips);

    uint8_t reported[protocol::kRxChipBytes] = {0};
    deliver(chips, chip_length, reported, sizeof(reported));
    protocol::Frame frame{};
    CHECK_FALSE(protocol::receive_m_band(reported, sizeof(reported), frame));
    int past_the_burst = 0;
    for (uint8_t i = protocol::kNoiseWindowBytes; i < protocol::kRxFrameBytes; i++)
        past_the_burst += __builtin_popcount(frame.erasures[i]);
    CHECK(past_the_burst > 0);
    CHECK_FALSE(protocol::framed_noise(frame));
}

TEST_CASE("air: a report shorter than the dwell reads is refused") {
    uint8_t reported[protocol::kRxChipBytes] = {0};
    protocol::Frame frame{};
    CHECK_FALSE(protocol::receive_m_band(reported, protocol::kRxChipBytes - 1, frame));
}

// The band a burst arrived in is what names its system, and the band is a fact
// the dwell knew when it armed. Guessing it downstream is what sent every
// O-band uplink frame into the M-band framer, where it failed and was counted
// as radio noise.
TEST_CASE("air: an O-band burst is named an uplink frame without being decoded") {
    uint8_t frame[protocol::kUplinkFrameBytes];
    fill(frame, protocol::kAdslFrameBytes, 9);
    protocol::Frame out{};
    CHECK(protocol::receive_burst(model::Band::O, frame, protocol::kUplinkFrameBytes, out) ==
          protocol::System::AdslUplink);
    // The codeword is eleven times longer than a Frame and stays where the
    // executor put it, so nothing was copied into out.
    CHECK(out.system == protocol::System::Unknown);
    CHECK(out.bytes == 0);
}

TEST_CASE("air: an O-band burst of the wrong length is not an uplink frame") {
    uint8_t frame[protocol::kUplinkFrameBytes] = {0};
    protocol::Frame out{};
    CHECK(protocol::receive_burst(model::Band::O, frame, protocol::kAdslFrameBytes, out) ==
          protocol::System::Unknown);
}

TEST_CASE("air: an M-band burst is still framed and named from its sync tail") {
    uint8_t payload[protocol::kAdslFrameBytes];
    fill(payload, sizeof(payload), 3);
    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_length =
        protocol::encode_m_band(protocol::kAlptasSyncWord, payload, sizeof(payload), chips);
    uint8_t reported[protocol::kRxChipBytes] = {0};
    deliver(chips, chip_length, reported, sizeof(reported));

    protocol::Frame out{};
    CHECK(protocol::receive_burst(model::Band::M, reported, sizeof(reported), out) ==
          protocol::System::Alptas);
    CHECK(out.bytes == protocol::kAlptasFrameBytes);
}

// The O-band framing a transmitter uses, round-tripped through the same sync
// strip the radio performs: encode and decode change together.
TEST_CASE("air: an O-band burst carries the sync word, the length byte and the codeword") {
    uint8_t frame[protocol::kUplinkFrameBytes];
    for (int i = 0; i < protocol::kUplinkFrameBytes; i++)
        frame[i] = static_cast<uint8_t>(i * 7 + 1);
    uint8_t burst[protocol::kUplinkBurstBytes] = {0};
    CHECK(protocol::encode_o_band(frame, burst) ==
          static_cast<size_t>(protocol::kUplinkBurstBytes));
    // §C.4.3, then §D.1.1: no Manchester on this band, so the bytes are the bytes.
    CHECK(burst[0] == 0x2D);
    CHECK(burst[1] == 0xD4);
    CHECK(burst[2] == protocol::kUplinkFrameBytes);
    CHECK(std::memcmp(burst + 3, frame, protocol::kUplinkFrameBytes) == 0);
}
