// Harness, not a test: the DF17 frames the clause cases send, built field by field.
#ifndef SKYBLIP_PROTOCOLS_ADSB_TEST_CONFORMANCE_REFERENCE_H
#define SKYBLIP_PROTOCOLS_ADSB_TEST_CONFORMANCE_REFERENCE_H

#include <cstdint>

#include "doctest/doctest.h"
#include "protocols/adsb/message.h"
#include "protocols/adsb/squitter.h"

namespace skyblip::adsb::conformance {

constexpr uint32_t kAddress = 0x4840D6;
constexpr uint8_t kAirborneCapability = 5;

struct Heard {
    wire::Status status{wire::Status::Invalid};
    Message message;
};

inline adsb::Squitter squitter(uint8_t type_code, uint8_t subtype = 0) {
    adsb::Squitter frame = adsb::squitter(kAddress, kAirborneCapability, type_code);
    adsb::set_message_bits(frame, 6, 8, subtype);
    return frame;
}

inline void put(adsb::Squitter& frame, uint8_t first, uint8_t last, uint64_t value) {
    adsb::set_message_bits(frame, first, last, value);
}

inline Heard hear_as_sent(const adsb::Squitter& frame) {
    Heard heard;
    heard.status = receive(frame.bytes, kFrameBytes, Reception{1000, -70, true}, heard.message);
    return heard;
}

inline Heard hear(adsb::Squitter frame) {
    adsb::seal(frame.bytes);
    return hear_as_sent(frame);
}

inline Message heard_ok(const adsb::Squitter& frame) {
    const Heard heard = hear(frame);
    REQUIRE(heard.status == wire::Status::Ok);
    return heard.message;
}

inline adsb::Squitter velocity(uint8_t subtype) {
    adsb::Squitter frame = squitter(19, subtype);
    if (subtype <= 2) {
        put(frame, 14, 14, 0);
        put(frame, 15, 24, 101);
        put(frame, 25, 25, 1);
        put(frame, 26, 35, 201);
    } else {
        put(frame, 14, 14, 1);
        put(frame, 15, 24, 256);
        put(frame, 25, 25, 1);
        put(frame, 26, 35, 301);
    }
    put(frame, 36, 36, 1);
    put(frame, 37, 37, 1);
    put(frame, 38, 46, 11);
    put(frame, 49, 49, 0);
    put(frame, 50, 56, 5);
    return frame;
}

}

#endif
