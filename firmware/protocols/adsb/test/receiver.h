#ifndef SKYBLIP_PROTOCOLS_ADSB_TEST_RECEIVER_H
#define SKYBLIP_PROTOCOLS_ADSB_TEST_RECEIVER_H

#include <cmath>
#include <cstdint>

#include "doctest/doctest.h"
#include "protocols/adsb/assembly.h"
#include "protocols/adsb/message.h"
#include "protocols/adsb/test/squitter.h"

namespace skyblip::adsb {

inline wire::Status adsb_hear(AdsbFrame frame, Message& message) {
    adsb_seal(frame.bytes);
    uint8_t chips[kChipBytes];
    adsb_chips(frame.bytes, chips);
    return receive(chips, sizeof(chips), Reception{0, -70}, message);
}

inline int hex_value(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return -1;
}

inline AdsbFrame frame_of(const char* text) {
    AdsbFrame frame;
    for (uint8_t i = 0; i < kFrameBytes; i++) {
        const int high = hex_value(text[2 * i]);
        REQUIRE(high >= 0);
        const int low = hex_value(text[2 * i + 1]);
        REQUIRE(low >= 0);
        frame.bytes[i] = static_cast<uint8_t>(high << 4 | low);
    }
    REQUIRE(text[2 * kFrameBytes] == 0);
    return frame;
}

namespace receiver {

constexpr uint32_t kAddress = 0x3C4D5E;
constexpr uint8_t kNoCapability = 0;
constexpr double kHomeLatitude = 48.1;
constexpr double kHomeLongitude = 8.1;
constexpr double kLatitude = 48.2;
constexpr double kLongitude = 8.2;
constexpr uint32_t kUtcS = 1700000000;

inline int32_t e7(double degrees) { return static_cast<int32_t>(std::lround(degrees * 1e7)); }

inline Reference reference_at(double latitude, double longitude) {
    Reference reference;
    reference.position = {e7(latitude), e7(longitude)};
    reference.valid = true;
    return reference;
}

inline double metres_from(const Track& heard, double latitude, double longitude) {
    const double north = (heard.position.latitude_1e7 / 1e7 - latitude) * 111320.0;
    const double east = (heard.position.longitude_1e7 / 1e7 - longitude) * 111320.0 *
                        std::cos(latitude * std::acos(-1.0) / 180);
    return std::hypot(north, east);
}

struct Bravo {
    Assembly assembly;
    Reference reference = reference_at(kHomeLatitude, kHomeLongitude);
    Track heard{};

    bool hear(const AdsbFrame& frame, uint32_t at_ms) {
        Message message;
        REQUIRE(adsb_hear(frame, message) == wire::Status::Ok);
        Receipt received;
        received.utc_s = kUtcS + at_ms / 1000;
        received.utc_ms = static_cast<uint16_t>(at_ms % 1000);
        return assembly.fold(message, at_ms, received, reference, heard);
    }

    bool hear_raw(const char* text, uint32_t at_ms) {
        Message message;
        uint8_t chips[kChipBytes];
        adsb_chips(frame_of(text).bytes, chips);
        REQUIRE(receive(chips, sizeof(chips), Reception{}, message) == wire::Status::Ok);
        return assembly.fold(message, at_ms, Receipt{}, reference, heard);
    }

    void fly(double latitude, double longitude, uint32_t at_ms, bool odd,
             int32_t altitude_ft = 3000, uint32_t address = kAddress) {
        REQUIRE(hear(
            adsb_airborne_position(address, kNoCapability, latitude, longitude, odd, altitude_ft),
            at_ms));
    }
};

}

}

#endif
