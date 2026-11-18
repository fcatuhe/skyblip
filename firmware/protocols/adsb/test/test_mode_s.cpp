// The Mode S replies a transponder sends without ADS-B, read into a Message.
#include <cstdint>

#include "doctest/doctest.h"
#include "protocols/adsb/message.h"
#include "protocols/adsb/squitter.h"
#include "protocols/adsb/test/receiver.h"

using namespace skyblip;
using namespace skyblip::adsb;
using skyblip::wire::Status;

namespace {

constexpr uint32_t kAddress = 0x4840D6;

struct Heard {
    Status status;
    Message message;
};

Heard hear(const Squitter& frame) {
    Heard heard{Status::Ok, {}};
    heard.status = receive(frame.bytes, kFrameBytes, Reception{1000, -64, true}, heard.message);
    return heard;
}

// INFO: fc 18nov26 the FIFO holds 14 bytes whatever the format, so a short reply arrives with noise
Squitter short_frame_of(const char* text) {
    Squitter frame;
    for (uint8_t i = 0; i < kShortFrameBytes; i++)
        frame.bytes[i] =
            static_cast<uint8_t>(hex_value(text[2 * i]) << 4 | hex_value(text[2 * i + 1]));
    REQUIRE(text[2 * kShortFrameBytes] == 0);
    for (uint8_t i = kShortFrameBytes; i < kFrameBytes; i++) frame.bytes[i] = 0xA5;
    return frame;
}

void flip_bit(Squitter& frame, uint8_t bit) {
    const uint8_t index = static_cast<uint8_t>(bit - 1);
    frame.bytes[index >> 3] =
        static_cast<uint8_t>(frame.bytes[index >> 3] ^ (0x80u >> (index & 7)));
}

}

TEST_CASE("adsb: an all-call reply carries its address in clear, verified when the parity is 0") {
    const Heard squitter = hear(all_call(kAddress, 5));
    REQUIRE(squitter.status == Status::Ok);
    CHECK(squitter.message.kind == Kind::AllCall);
    CHECK(squitter.message.downlink_format == 11);
    CHECK(squitter.message.address == kAddress);
    CHECK(squitter.message.address_verified);
    CHECK(squitter.message.capability == 5);
    CHECK(squitter.message.reception.rssi_dbm == -64);

    const Heard answer = hear(all_call(kAddress, 5, 0x13));
    REQUIRE(answer.status == Status::Ok);
    CHECK(answer.message.address == kAddress);
    CHECK_FALSE(answer.message.address_verified);
}

TEST_CASE("adsb: an all-call reply whose parity is no interrogator code is refused") {
    Squitter frame = all_call(kAddress, 5);
    flip_bit(frame, 20);
    CHECK(hear(frame).status == Status::Crc);
}

TEST_CASE("adsb: the Riddle's DF4 reads 36000 ft, and its address only through its parity") {
    const Heard heard = hear(short_frame_of("2000171806A983"));
    REQUIRE(heard.status == Status::Ok);
    CHECK(heard.message.kind == Kind::Reply);
    CHECK(heard.message.downlink_format == 4);
    CHECK(heard.message.reply.pressure_altitude_valid);
    CHECK(heard.message.reply.pressure_altitude_ft == 36000);
    CHECK(heard.message.address == 0x4CA7E8);
    CHECK_FALSE(heard.message.address_verified);
}

TEST_CASE("adsb: the Riddle's DF20 recovers 3C6DD0 from its parity and reads 38000 ft") {
    const Heard heard = hear(frame_of("A0001838CA380031440000F24177"));
    REQUIRE(heard.status == Status::Ok);
    CHECK(heard.message.kind == Kind::Reply);
    CHECK(heard.message.downlink_format == 20);
    CHECK(heard.message.address == 0x3C6DD0);
    CHECK(heard.message.reply.pressure_altitude_ft == 38000);
}

TEST_CASE("adsb: the Riddle's identity reply carries an address and no altitude") {
    const Heard heard = hear(short_frame_of("2A00516D492B80"));
    REQUIRE(heard.status == Status::Ok);
    CHECK(heard.message.kind == Kind::Reply);
    CHECK(heard.message.downlink_format == 5);
    CHECK(heard.message.address == 0x510AF9);
    CHECK_FALSE(heard.message.reply.pressure_altitude_valid);
}

TEST_CASE("adsb: every altitude reply format reads its AC field") {
    for (uint8_t format : {0, 4, 16, 20}) {
        const Heard heard = hear(altitude_reply(kAddress, 4500, format));
        REQUIRE(heard.status == Status::Ok);
        CHECK(heard.message.downlink_format == format);
        CHECK(heard.message.address == kAddress);
        CHECK(heard.message.reply.pressure_altitude_ft == 4500);
    }
}

TEST_CASE("adsb: a Gillham AC field reads in 100 ft steps, and a metric one reads no altitude") {
    // Gillham 000000100001 is 2700 ft in test_message.cpp, here with a zero M bit after A4.
    const Heard feet = hear(reply(kAddress, 4, 0b0000100100000));
    REQUIRE(feet.status == Status::Ok);
    CHECK(feet.message.reply.pressure_altitude_valid);
    CHECK(feet.message.reply.pressure_altitude_ft == 2700);

    const Heard metres = hear(reply(kAddress, 4, altitude_code(4500) | 0x40u));
    REQUIRE(metres.status == Status::Ok);
    CHECK_FALSE(metres.message.reply.pressure_altitude_valid);
}

TEST_CASE("adsb: a format the decoder does not read is unsupported, a damaged DF17 is crc") {
    Squitter extended = squitter(kAddress, 5, 4);
    seal(extended.bytes);
    Squitter non_transponder = extended;
    set_frame_bits(non_transponder, 1, 5, 18);
    CHECK(hear(non_transponder).status == Status::Unsupported);

    flip_bit(extended, 60);
    CHECK(hear(extended).status == Status::Crc);
}
