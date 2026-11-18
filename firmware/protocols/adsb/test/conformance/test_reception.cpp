// DO-260B 2.2.4 to 2.2.7, the decoding half: which frames are accepted, refused and handed on.
#include <cstdint>
#include <initializer_list>

#include "doctest/doctest.h"
#include "protocols/adsb/assembly.h"
#include "protocols/adsb/message.h"
#include "protocols/adsb/squitter.h"
#include "protocols/adsb/test/conformance/reference.h"
#include "protocols/adsb/test/receiver.h"

using namespace skyblip;
using namespace skyblip::adsb;
using skyblip::wire::Status;

namespace {

namespace standard = skyblip::adsb::conformance;

adsb::Squitter position() {
    return adsb::airborne_position(standard::kAddress, standard::kAirborneCapability, 48.2, 8.2,
                                   false, 3000);
}

adsb::Squitter sealed(adsb::Squitter frame) {
    adsb::seal(frame.bytes);
    return frame;
}

void flip_bit(adsb::Squitter& frame, uint8_t bit) {
    const uint8_t index = static_cast<uint8_t>(bit - 1);
    frame.bytes[index >> 3] =
        static_cast<uint8_t>(frame.bytes[index >> 3] ^ (0x80u >> (index & 7)));
}

Status receive_frame(const adsb::Squitter& frame, Message& message) {
    return receive(frame.bytes, kFrameBytes, Reception{}, message);
}

}

TEST_CASE("DO-260B 2.2.4.3.4.5: every valid DF17 message with a zero PI overlay is decoded") {
    for (const adsb::Squitter& frame :
         {position(), standard::velocity(1), standard::velocity(3),
          adsb::identification(standard::kAddress, 5, 4, 1, "KLM1023"),
          adsb::operational_status(standard::kAddress, 5, 2, 1),
          adsb::surface_position(standard::kAddress, 4, 48.1, 8.1, true, 20)})
        CHECK(standard::hear(frame).status == Status::Ok);
}

TEST_CASE(
    "DO-260B 2.2.4.3.4.7.3: the receiver decides 112 bits from their chips, the parity each") {
    adsb::Squitter frame = sealed(position());
    Message message;
    REQUIRE(receive_frame(frame, message) == Status::Ok);
    CHECK(kFrameBytes * 8 == 112);
    flip_bit(frame, 60);
    CHECK(receive_frame(frame, message) == Status::Crc);
}

// TODO: fc 17nov26 no doubtful bit reaches the decoder, until a receiver hands up its chip pairs
TEST_CASE("DO-260B 2.2.4.3.4.7.3: a frame with more than seven doubtful bits in a row is refused" *
          doctest::skip()) {
    FAIL(
        "not implemented: Bravo's LR2021 hands up no doubtful bit, so the decoder counts no run of "
        "them");
}

// INFO: fc 17nov26 2.2.4.4.2 binds these to classes A1S to A3, and Bravo answers to A0
TEST_CASE("DO-260B 2.2.4.4: enhanced squitter reception copes with overlapping Mode A/C fruit" *
          doctest::skip()) {
    FAIL("optional: the enhanced techniques bind classes A1S to A3, and Bravo's 100 tracks are A0");
}

// INFO: fc 17nov26 a rationale for the enhanced techniques, with nothing a receiver does
TEST_CASE("DO-260B 2.2.4.4.1: dense fruit is why enhanced reception exists" * doctest::skip()) {
    FAIL("prose: the need for the enhanced techniques, with nothing a receiver does");
}

// INFO: fc 17nov26 2.2.4.4.2 binds these to classes A1S to A3, and Bravo answers to A0
TEST_CASE("DO-260B 2.2.4.4.2: enhanced reception improves preamble, bit and error decisions" *
          doctest::skip()) {
    FAIL("optional: the enhanced techniques bind classes A1S to A3, and Bravo's 100 tracks are A0");
}

// INFO: fc 17nov26 2.2.4.5 c binds the enhanced error correction to classes A1S to A3
TEST_CASE("DO-260B 2.2.4.4.3: errors are corrected by the enhanced techniques" * doctest::skip()) {
    FAIL("optional: enhanced error correction binds classes A1S to A3, and Bravo answers to A0");
}

// INFO: fc 17nov26 2.2.4.5 c binds the enhanced error correction to classes A1S to A3
TEST_CASE("DO-260B 2.2.4.4.3.1: correction is conservative and brute force, as Appendix I says" *
          doctest::skip()) {
    FAIL("optional: enhanced error correction binds classes A1S to A3, and Bravo answers to A0");
}

TEST_CASE("DO-260B 2.2.4.4.3.2: no sliding window repairs a wrong bit, which is refused") {
    adsb::Squitter frame = sealed(position());
    flip_bit(frame, 60);
    Message message;
    CHECK(receive_frame(frame, message) == Status::Crc);
}

TEST_CASE("DO-260B 2.2.4.5: the parity is checked on every frame before it is read") {
    const adsb::Squitter frame = sealed(position());
    Message message;
    REQUIRE(receive_frame(frame, message) == Status::Ok);
    for (uint8_t bit : {5, 33, 88, 112}) {
        adsb::Squitter damaged = frame;
        flip_bit(damaged, bit);
        CHECK(receive_frame(damaged, message) == Status::Crc);
    }
}

// TODO: fc 17nov26 no frame is corrected, until #373 measures a single-bit flip on Bravo
TEST_CASE("DO-260B 2.2.4.5: a class A0 receiver corrects errors as DO-185B Appendix A A.3 does" *
          doctest::skip()) {
    FAIL(
        "not implemented: Bravo's LR2021 hands up no doubtful bit, so a frame that fails is "
        "refused, until issue #373 measures a single-bit flip");
}

TEST_CASE("DO-260B 2.2.6: the reception function hands each message on, decoded or refused") {
    Message message;
    CHECK(standard::hear(position()).status == Status::Ok);
    CHECK(standard::hear(standard::squitter(29)).status == Status::Unsupported);
    uint8_t frame[kFrameBytes]{};
    CHECK(receive(frame, kFrameBytes - 1, Reception{}, message) == Status::Invalid);
}

TEST_CASE("DO-260B 2.2.6.1: version 0, 1 and 2 formats decode, and a target keeps its version") {
    receiver::Bravo bravo;
    REQUIRE(bravo.hear(adsb::operational_status(receiver::kAddress, 0, 0, 0), 0));
    CHECK(bravo.heard.version_valid);
    CHECK(bravo.heard.version == 0);
    REQUIRE(bravo.hear(adsb::ground_velocity(receiver::kAddress, 0, 100, 0, 0, 0), 100));
    CHECK(bravo.heard.version == 0);
    REQUIRE(bravo.hear(adsb::operational_status(receiver::kAddress, 0, 2, 1), 200));
    REQUIRE(bravo.hear(adsb::ground_velocity(receiver::kAddress, 0, 100, 0, 0, 0), 300));
    CHECK(bravo.heard.version == 2);
    CHECK(bravo.heard.gva == 1);
}

TEST_CASE("DO-260B 2.2.6.1.1: a received message is its DF, its AA and its ME") {
    const Message message = standard::heard_ok(position());
    CHECK(message.address == standard::kAddress);
    CHECK(message.type_code == 11);
    CHECK(message.airborne_position.pressure_altitude_ft == 3000);
}

TEST_CASE("DO-260B 2.2.7: the message processor turns messages into a track") {
    receiver::Bravo bravo;
    bravo.fly(receiver::kLatitude, receiver::kLongitude, 0, false);
    bravo.fly(receiver::kLatitude, receiver::kLongitude, 1000, true);
    CHECK(bravo.heard.position_valid);
}

TEST_CASE("DO-260B 2.2.7.1: a reserved version is valid, and reserved bits are never read") {
    const Message reserved =
        standard::heard_ok(adsb::operational_status(standard::kAddress, 5, 5, 1));
    CHECK(reserved.operational_status.version == 5);
    CHECK_FALSE(reserved.operational_status.gva_valid);
    adsb::Squitter frame = standard::velocity(1);
    standard::put(frame, 47, 48, 3);
    CHECK(standard::heard_ok(frame).velocity.east_kt.value == 100);
}

TEST_CASE("DO-260B 2.2.7.1.1: a TYPE 0 message with no altitude is discarded") {
    Message message;
    CHECK(receive_sealed(adsb::squitter(receiver::kAddress, 0, 0), message) == Status::Unsupported);
}

// INFO: fc 17nov26 item b says may, and TYPE 0 is refused whole
TEST_CASE("DO-260B 2.2.7.1.1: a TYPE 0 altitude may refresh a tracked aircraft's altitude" *
          doctest::skip()) {
    FAIL("optional: item b says may, and a TYPE 0 message is refused with its altitude");
}
