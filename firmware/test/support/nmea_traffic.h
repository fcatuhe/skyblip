// Harness, not a test: an own ship, a target heard by one source, and the fields a sentence
// carries.
#ifndef SKYBLIP_TEST_SUPPORT_NMEA_TRAFFIC_H
#define SKYBLIP_TEST_SUPPORT_NMEA_TRAFFIC_H

#include <algorithm>
#include <cstdint>
#include <string>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"

namespace skyblip {

// The fields after the sentence name: one comma stands in front of each of them.
inline int data_fields(const std::string& sentence) {
    const std::string body = sentence.substr(0, sentence.find('*'));
    return static_cast<int>(std::count(body.begin(), body.end(), ','));
}

inline model::Ownship own_at(int32_t latitude_1e7, int32_t longitude_1e7, int32_t altitude_m) {
    model::Ownship own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.latitude_1e7 = latitude_1e7;
    own.longitude_1e7 = longitude_1e7;
    own.altitude_hae_mm = altitude_m * 1000;
    return own;
}

inline model::AircraftObservation heard(model::Source source, int32_t latitude_1e7,
                                        int32_t longitude_1e7, int32_t altitude_m) {
    model::AircraftObservation target{};
    target.position_valid = true;
    target.source = source;
    target.address = source == model::Source::Adsb ? 0x3C6586 : 0xDD8F12;
    target.address_mapping_table = source == model::Source::Adsb ? 0x05 : 0x06;
    target.aircraft_category = source == model::Source::Adsb ? 2 : 4;
    target.flight_state = 2;
    target.latitude_1e7 = latitude_1e7;
    target.longitude_1e7 = longitude_1e7;
    target.altitude_m = altitude_m;
    target.track_c9 = 128;
    target.speed_valid = true;
    target.speed_q = 248;
    target.climb_valid = true;
    target.climb_e8 = 40;
    return target;
}

}  // namespace skyblip

#endif
