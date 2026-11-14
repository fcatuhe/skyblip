// Harness, not a test: a Bravo receiver folding DF17 frames into an adsb::Assembly.
#ifndef SKYBLIP_TEST_SUPPORT_ADSB_RECEIVER_H
#define SKYBLIP_TEST_SUPPORT_ADSB_RECEIVER_H

#include <cmath>
#include <cstdint>

#include "core/events/stamp.h"
#include "core/model/aircraft.h"
#include "core/protocol/adsb_observation.h"
#include "core/util/hex.h"
#include "doctest/doctest.h"
#include "protocols/adsb/assembly.h"
#include "protocols/adsb/message.h"
#include "test/support/adsb_frames.h"

namespace skyblip::adsb_receiver {

constexpr uint32_t kAddress = 0x3C4D5E;
constexpr uint8_t kNoCapability = 0;
constexpr double kHomeLatitude = 48.1;
constexpr double kHomeLongitude = 8.1;
constexpr double kLatitude = 48.2;
constexpr double kLongitude = 8.2;
constexpr uint32_t kUtcS = 1700000000;

inline int32_t e7(double degrees) { return static_cast<int32_t>(std::lround(degrees * 1e7)); }

inline adsb::Reference reference_at(double latitude, double longitude) {
    adsb::Reference reference;
    reference.position = {e7(latitude), e7(longitude)};
    reference.valid = true;
    return reference;
}

inline double metres_from(const model::AircraftObservation& heard, double latitude,
                          double longitude) {
    const double north = (heard.latitude_1e7 / 1e7 - latitude) * 111320.0;
    const double east = (heard.longitude_1e7 / 1e7 - longitude) * 111320.0 *
                        std::cos(latitude * std::acos(-1.0) / 180);
    return std::hypot(north, east);
}

inline AdsbFrame frame_of(const char* text) {
    AdsbFrame frame;
    REQUIRE(hex::read(text, ByteSpan(frame.bytes)));
    return frame;
}

struct Bravo {
    adsb::Assembly assembly;
    adsb::Reference reference = reference_at(kHomeLatitude, kHomeLongitude);
    model::AircraftObservation heard{};

    bool hear(const AdsbFrame& frame, uint32_t at_ms) {
        adsb::Message message;
        REQUIRE(adsb_hear(frame, message) == Status::Ok);
        events::Stamp received;
        received.at_s = kUtcS + at_ms / 1000;
        received.into_ms = static_cast<uint16_t>(at_ms % 1000);
        return protocol::adsb_fold(assembly, message, at_ms, received, reference, heard);
    }

    bool hear_raw(const char* text, uint32_t at_ms) {
        adsb::Message message;
        uint8_t chips[adsb::kChipBytes];
        adsb_chips(frame_of(text).bytes, chips);
        REQUIRE(adsb::receive(chips, sizeof(chips), adsb::Reception{}, message) ==
                wire::Status::Ok);
        return protocol::adsb_fold(assembly, message, at_ms, events::Stamp{}, reference, heard);
    }

    void fly(double latitude, double longitude, uint32_t at_ms, bool odd,
             int32_t altitude_ft = 3000, uint32_t address = kAddress) {
        REQUIRE(hear(
            adsb_airborne_position(address, kNoCapability, latitude, longitude, odd, altitude_ft),
            at_ms));
    }
};

}

#endif
