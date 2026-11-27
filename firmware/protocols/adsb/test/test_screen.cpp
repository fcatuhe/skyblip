// The screen a radio thread runs before the queue: the decoder's own refusals, and nothing more.
#include <algorithm>
#include <cstdint>
#include <iterator>

#include "doctest/doctest.h"
#include "protocols/adsb/message.h"
#include "protocols/adsb/squitter.h"
#include "protocols/adsb/test/receiver.h"

using namespace skyblip;
using namespace skyblip::adsb;
using skyblip::wire::Status;

namespace {

constexpr uint32_t kAddress = 0x4840D6;

Squitter sealed(Squitter frame) {
    seal(frame.bytes);
    return frame;
}

void flip_bit(Squitter& frame, uint8_t bit) {
    const uint8_t index = static_cast<uint8_t>(bit - 1);
    frame.bytes[index >> 3] =
        static_cast<uint8_t>(frame.bytes[index >> 3] ^ (0x80u >> (index & 7)));
}

Status decode(const Squitter& frame) {
    Message message{};
    return receive(frame.bytes, kFrameBytes, Reception{0, -70, true}, message);
}

}

TEST_CASE("adsb screen: each format the decoder reads is kept under its own name" *
          doctest::test_suite("main")) {
    CHECK(screen(sealed(squitter(kAddress, 5, 4)).bytes) == Screened::Squitter);
    CHECK(screen(all_call(kAddress, 5).bytes) == Screened::AllCall);
    CHECK(screen(all_call(kAddress, 5, 0x13).bytes) == Screened::AllCall);
    for (uint8_t format : {0, 4, 16, 20})
        CHECK(screen(altitude_reply(kAddress, 4500, format).bytes) == Screened::Reply);
    for (uint8_t format : {5, 21})
        CHECK(screen(reply(kAddress, format, 0).bytes) == Screened::Reply);
}

TEST_CASE("adsb screen: a damaged DF17 or DF11 fails the parity, an unread format the format") {
    Squitter extended = sealed(squitter(kAddress, 5, 4));
    flip_bit(extended, 60);
    CHECK(screen(extended.bytes) == Screened::Parity);
    Squitter acquisition = all_call(kAddress, 5);
    flip_bit(acquisition, 20);
    CHECK(screen(acquisition.bytes) == Screened::Parity);
    Squitter non_transponder = sealed(squitter(kAddress, 5, 4));
    set_frame_bits(non_transponder, 1, 5, 18);
    CHECK(screen(non_transponder.bytes) == Screened::Format);
    for (uint8_t format : {1, 2, 3, 6, 7, 8, 9, 10, 12, 13, 14, 15, 19, 22, 23, 24, 31}) {
        Squitter frame;
        set_frame_bits(frame, 1, 5, format);
        CHECK(screen(frame.bytes) == Screened::Format);
    }
}

// A screen that refused what receive() reads would lose traffic no counter shows.
TEST_CASE("adsb screen: it refuses only what receive() refuses, over every format and noise") {
    uint32_t state = 0x2545F491u;
    for (int round = 0; round < 20000; round++) {
        Squitter frame;
        for (uint8_t& byte : frame.bytes) {
            state = state * 1664525u + 1013904223u;
            byte = static_cast<uint8_t>(state >> 24);
        }
        if (round % 4 == 1) seal(frame.bytes);
        if (round % 4 == 2) frame = all_call(state, 5, static_cast<uint8_t>(state & 0x7F));
        const Screened screened = screen(frame.bytes);
        const Status status = decode(frame);
        CAPTURE(round);
        if (status == Status::Ok) CHECK(kept(screened));
        if (screened == Screened::Parity) CHECK(status == Status::Crc);
        if (screened == Screened::Format) CHECK(status == Status::Unsupported);
    }
}

TEST_CASE("adsb screen: one flipped bit past the DF field is repaired to the sound frame") {
    const Squitter sound = sealed(squitter(kAddress, 5, 4));
    Squitter untouched = sound;
    CHECK(repair_bits(untouched.bytes) == 0);
    for (uint8_t bit = 6; bit <= 112; bit++) {
        Squitter damaged = sound;
        flip_bit(damaged, bit);
        CAPTURE(bit);
        REQUIRE(repair_bits(damaged.bytes) == 1);
        CHECK(std::equal(std::begin(damaged.bytes), std::end(damaged.bytes), sound.bytes));
    }
}

// Every pair of the 107 bits past the DF field has a syndrome of its own, so none is repaired
// wrong.
TEST_CASE("adsb screen: any two flipped bits past the DF field are repaired to the sound frame") {
    const Squitter sound = sealed(squitter(kAddress, 5, 4));
    for (uint8_t first = 6; first <= 112; first++) {
        for (uint8_t second = static_cast<uint8_t>(first + 1); second <= 112; second++) {
            Squitter damaged = sound;
            flip_bit(damaged, first);
            flip_bit(damaged, second);
            CAPTURE(first);
            CAPTURE(second);
            REQUIRE(repair_bits(damaged.bytes) == 2);
            REQUIRE(std::equal(std::begin(damaged.bytes), std::end(damaged.bytes), sound.bytes));
        }
    }
}

// DO-260B 2.2.4.3.4.7.3.a: a repair never touches the DF, and three errors are beyond two bits.
TEST_CASE("adsb screen: three flipped bits, a DF bit and an all-call are never repaired") {
    const Squitter sound = sealed(squitter(kAddress, 5, 4));
    Squitter thrice = sound;
    flip_bit(thrice, 40);
    flip_bit(thrice, 70);
    flip_bit(thrice, 100);
    const Squitter before = thrice;
    CHECK(repair_bits(thrice.bytes) == 0);
    CHECK(std::equal(std::begin(thrice.bytes), std::end(thrice.bytes), before.bytes));
    Squitter format = sound;
    flip_bit(format, 5);
    CHECK(repair_bits(format.bytes) == 0);
    Squitter acquisition = all_call(kAddress, 5);
    flip_bit(acquisition, 20);
    CHECK(repair_bits(acquisition.bytes) == 0);
}
