// Harness, not a test: the DF17 frames a transmitter sends, heard through the real decoder.
#ifndef SKYBLIP_TEST_SUPPORT_ADSB_FRAMES_H
#define SKYBLIP_TEST_SUPPORT_ADSB_FRAMES_H

#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/protocol/adsb.h"
#include "core/util/result.h"
#include "test/support/adsb_chips.h"

namespace skyblip {

struct AdsbFrame {
    uint8_t bytes[protocol::kAdsbFrameBytes]{};
};

inline void adsb_set_message_bits(AdsbFrame& frame, uint8_t first, uint8_t last, uint64_t value) {
    for (uint8_t bit = first; bit <= last; bit++) {
        const uint8_t index = static_cast<uint8_t>(32 + bit - 1);
        const uint8_t mask = static_cast<uint8_t>(0x80u >> (index & 7));
        const bool set = (value >> (last - bit)) & 1u;
        frame.bytes[index >> 3] = static_cast<uint8_t>(set ? frame.bytes[index >> 3] | mask
                                                           : frame.bytes[index >> 3] & ~mask);
    }
}

inline AdsbFrame adsb_frame(uint32_t address, uint8_t capability, uint8_t type_code) {
    AdsbFrame frame;
    frame.bytes[0] = static_cast<uint8_t>((17 << 3) | (capability & 7));
    frame.bytes[1] = static_cast<uint8_t>(address >> 16);
    frame.bytes[2] = static_cast<uint8_t>(address >> 8);
    frame.bytes[3] = static_cast<uint8_t>(address);
    adsb_set_message_bits(frame, 1, 5, type_code);
    return frame;
}

inline Status adsb_hear(AdsbFrame frame, protocol::AdsbMessage& message) {
    adsb_seal(frame.bytes);
    uint8_t chips[protocol::kAdsbChipBytes];
    adsb_chips(frame.bytes, chips);
    return protocol::adsb_receive(chips, sizeof(chips), protocol::AdsbReception{0, -70}, message);
}

// INFO: fc 10nov26 the closed form of DO-260B draft A.1.7.2 d, so the decoder's table is checked
inline int adsb_longitude_zones(double latitude) {
    const double absolute = std::fabs(latitude);
    if (absolute == 0) return 59;
    if (absolute == 87) return 2;
    if (absolute > 87) return 1;
    const double pi = std::acos(-1.0);
    const double cosine = std::cos(pi / 180 * absolute);
    return static_cast<int>(
        std::floor(2 * pi / std::acos(1 - (1 - std::cos(pi / 30)) / (cosine * cosine))));
}

// INFO: fc 10nov26 DO-260B draft A.1.7.3 in floating point, sending 17 of the 19 surface bits
inline protocol::AdsbCpr adsb_cpr_encode(double latitude, double longitude, bool odd,
                                         bool surface) {
    const double turn = 360.0;
    const double steps = surface ? 524288.0 : 131072.0;
    const int parity = odd ? 1 : 0;
    const auto modulo = [](double x, double y) { return x - y * std::floor(x / y); };
    const double latitude_zone = turn / (60 - parity);
    const double latitude_cpr =
        std::floor(steps * modulo(latitude, latitude_zone) / latitude_zone + 0.5);
    const double encoded_latitude =
        latitude_zone * (latitude_cpr / steps + std::floor(latitude / latitude_zone));
    const int zones = adsb_longitude_zones(encoded_latitude) - parity;
    const double longitude_zone = zones > 0 ? turn / zones : turn;
    const double longitude_cpr =
        std::floor(steps * modulo(longitude, longitude_zone) / longitude_zone + 0.5);
    protocol::AdsbCpr cpr;
    cpr.odd = odd;
    cpr.latitude_cpr = static_cast<uint32_t>(latitude_cpr) & 0x1FFFF;
    cpr.longitude_cpr = static_cast<uint32_t>(longitude_cpr) & 0x1FFFF;
    return cpr;
}

inline void adsb_set_cpr(AdsbFrame& frame, const protocol::AdsbCpr& cpr) {
    adsb_set_message_bits(frame, 22, 22, cpr.odd ? 1 : 0);
    adsb_set_message_bits(frame, 23, 39, cpr.latitude_cpr);
    adsb_set_message_bits(frame, 40, 56, cpr.longitude_cpr);
}

// INFO: fc 10nov26 a Q-bit altitude counts 25 ft steps from -1000 ft, with the Q bit after 4 bits
inline uint32_t adsb_altitude_field(int32_t feet) {
    const uint32_t steps = static_cast<uint32_t>((feet + 1000 + 12) / 25);
    return ((steps >> 4) << 5) | 0x10u | (steps & 0xFu);
}

inline AdsbFrame adsb_airborne_position(uint32_t address, uint8_t capability, double latitude,
                                        double longitude, bool odd, int32_t altitude_ft,
                                        bool gnss_height = false) {
    AdsbFrame frame = adsb_frame(address, capability, gnss_height ? 20 : 11);
    adsb_set_message_bits(frame, 9, 20, adsb_altitude_field(altitude_ft));
    adsb_set_cpr(frame, adsb_cpr_encode(latitude, longitude, odd, false));
    return frame;
}

inline AdsbFrame adsb_surface_position(uint32_t address, uint8_t capability, double latitude,
                                       double longitude, bool odd, uint8_t movement = 1) {
    AdsbFrame frame = adsb_frame(address, capability, 7);
    adsb_set_message_bits(frame, 6, 12, movement);
    adsb_set_cpr(frame, adsb_cpr_encode(latitude, longitude, odd, true));
    return frame;
}

inline uint64_t adsb_signed_field(int32_t value, int32_t step, uint8_t width) {
    const uint64_t sign = value < 0 ? 1u : 0u;
    const uint64_t code = static_cast<uint64_t>((value < 0 ? -value : value) / step + 1);
    return (sign << width) | code;
}

inline AdsbFrame adsb_ground_velocity(uint32_t address, uint8_t capability, int32_t east_kt,
                                      int32_t north_kt, int32_t climb_fpm,
                                      int32_t gnss_minus_pressure_ft) {
    AdsbFrame frame = adsb_frame(address, capability, 19);
    adsb_set_message_bits(frame, 6, 8, 1);
    adsb_set_message_bits(frame, 14, 24, adsb_signed_field(east_kt, 1, 10));
    adsb_set_message_bits(frame, 25, 35, adsb_signed_field(north_kt, 1, 10));
    adsb_set_message_bits(frame, 37, 46, adsb_signed_field(climb_fpm, 64, 9));
    adsb_set_message_bits(frame, 49, 56, adsb_signed_field(gnss_minus_pressure_ft, 25, 7));
    return frame;
}

inline AdsbFrame adsb_air_velocity(uint32_t address, uint8_t capability, uint16_t heading_c10,
                                   int32_t airspeed_kt, int32_t climb_fpm) {
    AdsbFrame frame = adsb_frame(address, capability, 19);
    adsb_set_message_bits(frame, 6, 8, 3);
    adsb_set_message_bits(frame, 14, 14, 1);
    adsb_set_message_bits(frame, 15, 24, heading_c10);
    adsb_set_message_bits(frame, 25, 25, 1);
    adsb_set_message_bits(frame, 26, 35, static_cast<uint64_t>(airspeed_kt + 1));
    adsb_set_message_bits(frame, 36, 36, 1);
    adsb_set_message_bits(frame, 37, 46, adsb_signed_field(climb_fpm, 64, 9));
    return frame;
}

inline AdsbFrame adsb_identification(uint32_t address, uint8_t capability, uint8_t type_code,
                                     uint8_t category, const char* callsign) {
    static constexpr char kCharacters[] =
        "?ABCDEFGHIJKLMNOPQRSTUVWXYZ????? ???????????????0123456789??????";
    AdsbFrame frame = adsb_frame(address, capability, type_code);
    adsb_set_message_bits(frame, 6, 8, category);
    const size_t length = std::strlen(callsign);
    for (uint8_t i = 0; i < protocol::kAdsbCallsignCharacters; i++) {
        const char character = i < length ? callsign[i] : ' ';
        const uint8_t code =
            static_cast<uint8_t>(std::strchr(kCharacters, character) - kCharacters);
        adsb_set_message_bits(frame, static_cast<uint8_t>(9 + 6 * i),
                              static_cast<uint8_t>(14 + 6 * i), code);
    }
    return frame;
}

inline AdsbFrame adsb_operational_status(uint32_t address, uint8_t capability, uint8_t version,
                                         uint8_t gva) {
    AdsbFrame frame = adsb_frame(address, capability, 31);
    adsb_set_message_bits(frame, 41, 43, version);
    adsb_set_message_bits(frame, 49, 50, gva);
    return frame;
}

}

#endif
