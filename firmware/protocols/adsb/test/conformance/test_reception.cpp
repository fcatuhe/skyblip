// DO-260B 2.2.4 to 2.2.7, the decoding half: which frames are accepted, repaired and handed on.
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

AdsbFrame position() {
    return adsb_airborne_position(standard::kAddress, standard::kAirborneCapability, 48.2, 8.2,
                                  false, 3000);
}

void sealed_chips(AdsbFrame frame, uint8_t* chips) {
    adsb_seal(frame.bytes);
    adsb_chips(frame.bytes, chips);
}

// INFO: fc 17nov26 bit b rides chips 2b-10 and 2b-9, and a doubtful pair reads its second chip
void make_doubtful(uint8_t* chips, uint8_t frame_bit) {
    const uint16_t first = static_cast<uint16_t>(2 * (frame_bit - 5));
    const uint16_t second = static_cast<uint16_t>(first + 1);
    const bool value = (chips[second >> 3] >> (7 - (second & 7))) & 1u;
    const uint8_t mask = static_cast<uint8_t>(0x80u >> (first & 7));
    chips[first >> 3] =
        static_cast<uint8_t>(value ? chips[first >> 3] | mask : chips[first >> 3] & ~mask);
}

void flip_bit(uint8_t* chips, uint8_t frame_bit) {
    for (uint16_t chip : {static_cast<uint16_t>(2 * (frame_bit - 5)),
                          static_cast<uint16_t>(2 * (frame_bit - 5) + 1)})
        chips[chip >> 3] = static_cast<uint8_t>(chips[chip >> 3] ^ (0x80u >> (chip & 7)));
}

Status receive_chips(const uint8_t* chips, Message& message) {
    return receive(chips, kChipBytes, Reception{}, message);
}

}

TEST_CASE("DO-260B 2.2.4.3.4.5: every valid DF17 message with a zero PI overlay is decoded") {
    for (const AdsbFrame& frame :
         {position(), standard::velocity(1), standard::velocity(3),
          adsb_identification(standard::kAddress, 5, 4, 1, "KLM1023"),
          adsb_operational_status(standard::kAddress, 5, 2, 1),
          adsb_surface_position(standard::kAddress, 4, 48.1, 8.1, true, 20)})
        CHECK(standard::hear(frame).status == Status::Ok);
}

TEST_CASE("DO-260B 2.2.4.3.4.7.3: each bit is the comparison of its two chips, 112 to a frame") {
    uint8_t chips[kChipBytes];
    sealed_chips(position(), chips);
    Message message;
    REQUIRE(receive_chips(chips, message) == Status::Ok);
    CHECK(kFrameBytes * 8 == 112);
    CHECK(kChipBytes * 8 + 8 == 2 * 112);
    flip_bit(chips, 60);
    CHECK(receive_chips(chips, message) == Status::Crc);
}

// TODO: fc 17nov26 doubtful pairs serve the repair only, until the decoder refuses a run of them
TEST_CASE("DO-260B 2.2.4.3.4.7.3: a frame with more than seven doubtful bits in a row is refused" *
          doctest::skip()) {
    uint8_t chips[kChipBytes];
    sealed_chips(position(), chips);
    for (uint8_t bit = 40; bit < 48; bit++) make_doubtful(chips, bit);
    Message message;
    CHECK(receive_chips(chips, message) != Status::Ok);
    FAIL("not implemented: a frame whose parity holds is accepted whatever its doubtful run");
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

TEST_CASE("DO-260B 2.2.4.4.3: errors are corrected only where the chips doubt the bit") {
    uint8_t chips[kChipBytes];
    sealed_chips(position(), chips);
    flip_bit(chips, 60);
    make_doubtful(chips, 60);
    Message message;
    CHECK(receive_chips(chips, message) == Status::Ok);
    CHECK(message.address == standard::kAddress);
}

TEST_CASE("DO-260B 2.2.4.4.3.1: correction is conservative, one doubtful bit and never two") {
    uint8_t chips[kChipBytes];
    sealed_chips(position(), chips);
    flip_bit(chips, 60);
    make_doubtful(chips, 60);
    flip_bit(chips, 70);
    make_doubtful(chips, 70);
    Message message;
    CHECK(receive_chips(chips, message) == Status::Crc);
}

TEST_CASE("DO-260B 2.2.4.4.3.2: no sliding window repairs a bit the chips did not doubt") {
    uint8_t chips[kChipBytes];
    sealed_chips(position(), chips);
    flip_bit(chips, 60);
    Message message;
    CHECK(receive_chips(chips, message) == Status::Crc);
}

TEST_CASE("DO-260B 2.2.4.5: the parity is checked on every frame before it is read") {
    uint8_t chips[kChipBytes];
    sealed_chips(position(), chips);
    Message message;
    REQUIRE(receive_chips(chips, message) == Status::Ok);
    for (uint8_t bit : {5, 33, 88, 112}) {
        uint8_t damaged[kChipBytes];
        for (uint8_t i = 0; i < kChipBytes; i++) damaged[i] = chips[i];
        flip_bit(damaged, bit);
        CHECK(receive_chips(damaged, message) == Status::Crc);
    }
}

// TODO: fc 17nov26 the A0 correction is unchecked, until DO-185B Appendix A is in hand
TEST_CASE("DO-260B 2.2.4.5: a class A0 receiver corrects errors as DO-185B Appendix A A.3 does" *
          doctest::skip()) {
    FAIL("not implemented: the repair of one doubtful bit was not compared with DO-185B A.3");
}

TEST_CASE("DO-260B 2.2.6: the reception function hands each message on, decoded or refused") {
    Message message;
    CHECK(standard::hear(position()).status == Status::Ok);
    CHECK(standard::hear(standard::squitter(29)).status == Status::Unsupported);
    uint8_t chips[kChipBytes]{};
    CHECK(receive(chips, kChipBytes - 1, Reception{}, message) == Status::Invalid);
}

TEST_CASE("DO-260B 2.2.6.1: version 0, 1 and 2 formats decode, and a target keeps its version") {
    receiver::Bravo bravo;
    REQUIRE(bravo.hear(adsb_operational_status(receiver::kAddress, 0, 0, 0), 0));
    CHECK(bravo.heard.version_valid);
    CHECK(bravo.heard.version == 0);
    REQUIRE(bravo.hear(adsb_ground_velocity(receiver::kAddress, 0, 100, 0, 0, 0), 100));
    CHECK(bravo.heard.version == 0);
    REQUIRE(bravo.hear(adsb_operational_status(receiver::kAddress, 0, 2, 1), 200));
    REQUIRE(bravo.hear(adsb_ground_velocity(receiver::kAddress, 0, 100, 0, 0, 0), 300));
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
        standard::heard_ok(adsb_operational_status(standard::kAddress, 5, 5, 1));
    CHECK(reserved.operational_status.version == 5);
    CHECK_FALSE(reserved.operational_status.gva_valid);
    AdsbFrame frame = standard::velocity(1);
    standard::put(frame, 47, 48, 3);
    CHECK(standard::heard_ok(frame).velocity.east_kt.value == 100);
}

TEST_CASE("DO-260B 2.2.7.1.1: a TYPE 0 message with no altitude is discarded") {
    Message message;
    CHECK(adsb_hear(adsb_frame(receiver::kAddress, 0, 0), message) == Status::Unsupported);
}

// INFO: fc 17nov26 item b says may, and TYPE 0 is refused whole
TEST_CASE("DO-260B 2.2.7.1.1: a TYPE 0 altitude may refresh a tracked aircraft's altitude" *
          doctest::skip()) {
    FAIL("optional: item b says may, and a TYPE 0 message is refused with its altitude");
}
