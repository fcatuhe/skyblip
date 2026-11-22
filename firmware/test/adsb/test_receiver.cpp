// DO-260B reception clauses that need Bravo's radio, clock and sync around the decoder.
#include <cstdint>

#include "core/events/stamp.h"
#include "doctest/doctest.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "hardware/parts/lr2021/model.h"
#include "protocols/adsb/assembly.h"
#include "protocols/adsb/message.h"
#include "protocols/adsb/squitter.h"
#include "test/support/lr2021_rig.h"

using namespace skyblip;
using parts::Lr2021EventType;
namespace chip = skyblip::parts::lr2021;

namespace {

constexpr uint32_t kAddress = 0x4840D6;

AdsbAir identification() { return adsb_air(adsb::squitter(kAddress, 5, 4)); }

bool framed(const AdsbAir& air) {
    models::Lr2021 part;
    parts::Lr2021 radio = lr2021_receiving(part);
    if (!part.receive_air(air.chips, sizeof(air.chips))) return false;
    uint8_t frame[adsb::kFrameBytes]{};
    return radio.poll(frame, sizeof(frame)).type == Lr2021EventType::Frame;
}

}

TEST_CASE("DO-260B 2.2.4: Bravo's receiver listens on 1090 MHz at two chips a microsecond" *
          doctest::test_suite("main")) {
    models::Lr2021 part;
    REQUIRE(lr2021_for(part).begin(chip::kDetectionThresholdDbm) == Status::Ok);
    CHECK(part.frequency_hz == 1090000000);
    CHECK(part.bitrate_bps == 2000000);
    CHECK(part.payload_bytes == adsb::kFrameBytes);
}

TEST_CASE("DO-260B 2.2.4.3: Bravo's receiver is its own, shared with no TCAS") {
    models::Lr2021 part;
    parts::Lr2021 radio = lr2021_receiving(part);
    CHECK(radio.mode() == parts::Lr2021Mode::Receive);
    CHECK(part.count(chip::kSetRx) == 1);
}

TEST_CASE("DO-260B 2.2.4.3.1: a message in band is accepted and handed up") {
    CHECK(framed(identification()));
}

// TODO: fc 17nov26 the LR2021 has a fixed OOK threshold, until a DMTL is built on its RSSI
TEST_CASE("DO-260B 2.2.4.3.3: a single receiver sets a dynamic minimum trigger level" *
          doctest::skip()) {
    CHECK(chip::kDetectionThresholdDbm == -110);
    FAIL("not implemented: the OOK detection threshold is fixed at -110 dBm, with no DMTL");
}

TEST_CASE("DO-260B 2.2.4.3.4: a message is framed by its preamble and its first four bits") {
    CHECK(framed(identification()));
    AdsbAir noise = identification();
    noise.chips[0] = 0x00;
    CHECK_FALSE(framed(noise));
}

TEST_CASE("DO-260B 2.2.4.3.4.7: a framed burst is a message the decoder reads") {
    models::Lr2021 part;
    parts::Lr2021 radio = lr2021_receiving(part);
    const AdsbAir air = identification();
    REQUIRE(part.receive_air(air.chips, sizeof(air.chips), -61));
    uint8_t frame[adsb::kFrameBytes]{};
    REQUIRE(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::Frame);
    adsb::Message message{};
    REQUIRE(adsb::receive(frame, sizeof(frame), adsb::Reception{}, message) == wire::Status::Ok);
    CHECK(message.address == kAddress);
}

TEST_CASE("DO-260B 2.2.4.3.4.7.2: the preamble is four pulses at 0, 1, 3.5 and 4.5 us") {
    CHECK(chip::kAdsbPreambleChips == 16);
    for (int chip_index : {0, 2, 7, 9})
        CHECK(((chip::kAdsbPreamblePattern >> chip_index) & 1u) == 1u);
    unsigned pulses = 0;
    for (int chip_index = 0; chip_index < chip::kAdsbPreambleChips; chip_index++)
        pulses += (chip::kAdsbPreamblePattern >> chip_index) & 1u;
    CHECK(pulses == 4u);
    CHECK(framed(identification()));
}

TEST_CASE("DO-260B 2.2.4.3.5: the driver receives three modeled bursts after one SetRx") {
    models::Lr2021 part;
    parts::Lr2021 radio = lr2021_receiving(part);
    uint8_t frame[adsb::kFrameBytes]{};
    for (int i = 0; i < 3; i++) {
        const AdsbAir air = identification();
        REQUIRE(part.receive_air(air.chips, sizeof(air.chips)));
        REQUIRE(radio.poll(frame, sizeof(frame)).type == Lr2021EventType::Frame);
    }
    CHECK(part.count(chip::kSetRx) == 1);
}

TEST_CASE("DO-260B 2.2.8.5: every frame is dated by the instant the radio reported it") {
    const uint64_t edge_us = 5000000;
    const events::Stamp stamp = events::stamp_of(edge_us + 345678, edge_us, true, 1700000000);
    CHECK(stamp.phase_valid);
    CHECK(stamp.at_s == 1700000000);
    CHECK(stamp.into_ms == 345);
}

TEST_CASE("DO-260B 2.2.8.5.1: with a PPS the date is UTC to the millisecond, finer than 1/128 s") {
    const uint64_t edge_us = 9000000;
    const events::Stamp late = events::stamp_of(edge_us + 1999000, edge_us, true, 1700000300);
    CHECK(late.at_s == 1700000301);
    CHECK(late.into_ms == 999);
    const events::Stamp next = events::stamp_of(edge_us + 1998000, edge_us, true, 1700000300);
    CHECK(late.into_ms - next.into_ms == 1);
}

TEST_CASE(
    "DO-260B 2.2.8.5.2: without a PPS each frame is timed on the receiver's millisecond clock") {
    const events::Stamp unlocked = events::stamp_of(12345678, 0, false, 1700000000);
    CHECK_FALSE(unlocked.phase_valid);
    adsb::Assembly assembly;
    adsb::Message message{};
    adsb::Squitter frame = adsb::squitter(kAddress, 5, 4);
    adsb::seal(frame.bytes);
    REQUIRE(adsb::receive(frame.bytes, adsb::kFrameBytes, adsb::Reception{}, message) ==
            wire::Status::Ok);
    adsb::Track track;
    REQUIRE(assembly.fold(message, static_cast<uint32_t>(12345678 / 1000), adsb::Receipt{},
                          adsb::Reference{}, track));
    CHECK(track.at_ms == 12345);
}
