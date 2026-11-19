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

TEST_CASE("DO-260B 2.2.4: Bravo's receiver listens on 1090 MHz at two chips a microsecond") {
    models::Lr2021 part;
    REQUIRE(lr2021_for(part).begin(chip::kDetectionThresholdDbm) == Status::Ok);
    CHECK(part.frequency_hz == 1090000000);
    CHECK(part.bitrate_bps == 2000000);
    CHECK(part.payload_bytes == adsb::kFrameBytes);
}

// TODO: fc 17nov26 Bravo's MTL is not measured, until the #172 bench run gives one
TEST_CASE("DO-260B 2.2.4.1: the MTL is the level at which 90% of messages are received" *
          doctest::skip()) {
    FAIL("unmeasured: no bench has found the level at which Bravo receives 90% of messages (#172)");
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

// TODO: fc 17nov26 Bravo's MTL is not measured, until the #172 bench run gives one
TEST_CASE("DO-260B 2.2.4.3.1.1: a class A0 receiver's MTL is -72 dBm or better" * doctest::skip()) {
    FAIL("unmeasured: the -72 dBm of Table 2-79 has not been measured at Bravo's antenna (#172)");
}

// INFO: fc 17nov26 the clause binds classes A1S to A3, and Bravo's 100 tracks are A0
TEST_CASE("DO-260B 2.2.4.3.1.2: a preamble 3 dB stronger re-triggers the receiver" *
          doctest::skip()) {
    FAIL("optional: re-triggering binds classes A1S to A3, and Bravo's 100 tracks are A0");
}

// TODO: fc 17nov26 out-of-band rejection is unmeasured, until a bench run with a signal generator
TEST_CASE("DO-260B 2.2.4.3.2: off 1090 MHz a message needs 3 to 60 dB more to be received" *
          doctest::skip()) {
    FAIL("unmeasured: Table 2-80's rejection has not been measured on the LR2021 (#172)");
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

// TODO: fc 17nov26 the LR2021 has a fixed OOK threshold, until a DMTL is built on its RSSI
TEST_CASE("DO-260B 2.2.4.3.4.1: a strong pulse raises the threshold to 6 dB under it" *
          doctest::skip()) {
    FAIL("not implemented: the OOK detection threshold is fixed at -110 dBm, with no DMTL");
}

// TODO: fc 17nov26 the LR2021 has a fixed OOK threshold, until a DMTL is built on its RSSI
TEST_CASE("DO-260B 2.2.4.3.4.2: pulses under 0.3 us do not move the DMTL" * doctest::skip()) {
    FAIL("not implemented: there is no DMTL for a narrow pulse to move");
}

// TODO: fc 17nov26 the LR2021 has a fixed OOK threshold, until a DMTL is built on its RSSI
TEST_CASE("DO-260B 2.2.4.3.4.3: TACAN and DME pulses do not move the DMTL" * doctest::skip()) {
    FAIL("not implemented: there is no DMTL for a TACAN or DME pulse to move");
}

// INFO: fc 17nov26 the clause points back to the transmitter's pulse shape, 2.2.2.2.1
TEST_CASE("DO-260B 2.2.4.3.4.4: received pulses are shaped as 2.2.2.2.1 transmits them" *
          doctest::skip()) {
    FAIL("prose: a pointer to the transmitter's pulse characteristics");
}

// INFO: fc 17nov26 the clause points back to the message formats, 2.2.3.2.3 to 2.2.3.2.7
TEST_CASE("DO-260B 2.2.4.3.4.6: the received signals carry the formats of 2.2.3.2.3 to 2.2.3.2.7" *
          doctest::skip()) {
    FAIL("prose: a pointer to the message formats, which the component's half reads");
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

// TODO: fc 17nov26 the LR2021 decides bits and reports no edges, until a part that does replaces it
TEST_CASE("DO-260B 2.2.4.3.4.7.1: pulses are detected on their leading edges" * doctest::skip()) {
    FAIL(
        "not implemented: the LR2021 decides each bit from its chip pair and reports no pulse "
        "edges");
}

TEST_CASE("DO-260B 2.2.4.3.4.7.2: the preamble is four pulses at 0, 1, 3.5 and 4.5 us") {
    CHECK(chip::kAdsbPreambleChips == 16);
    for (int chip_index : {0, 2, 7, 9})
        CHECK(((chip::kAdsbPreamblePattern >> chip_index) & 1u) == 1u);
    int pulses = 0;
    for (int chip_index = 0; chip_index < chip::kAdsbPreambleChips; chip_index++)
        pulses += (chip::kAdsbPreamblePattern >> chip_index) & 1u;
    CHECK(pulses == 4);
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

// TODO: fc 17nov26 the delivery time is unmeasured, until a bench times the IRQ to the fold
TEST_CASE("DO-260B 2.2.6.1.2: a message is handed on within 2 ms of its last bit" *
          doctest::skip()) {
    FAIL("unmeasured: the time from the last chip to the fold has not been measured on Bravo");
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

// TODO: fc 18nov26 the decoder refuses DF18 as unsupported, until it reads TIS-B
TEST_CASE("DO-260B 2.2.17.4: TIS-B messages are decoded into reports" * doctest::skip()) {
    FAIL("not implemented: TIS-B rides DF18, which the decoder refuses as unsupported");
}

// TODO: fc 18nov26 the decoder refuses DF18 as unsupported, until it reads ADS-R
TEST_CASE("DO-260B 2.2.18.4: ADS-R messages are decoded" * doctest::skip()) {
    FAIL("not implemented: ADS-R rides DF18 CF 6, which the decoder refuses as unsupported");
}

// TODO: fc 18nov26 the decoder refuses DF18 as unsupported, until it reads ADS-R
TEST_CASE("DO-260B 2.2.18.5: ADS-R reports are kept apart from ADS-B reports" * doctest::skip()) {
    FAIL("not implemented: no ADS-R message is ever heard, so no ADS-R report is kept");
}
