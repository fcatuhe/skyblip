#ifndef SKYBLIP_PROTOCOLS_ADSB_SQUITTER_H
#define SKYBLIP_PROTOCOLS_ADSB_SQUITTER_H

#include <cmath>
#include <cstdint>
#include <cstring>

#include "protocols/adsb/message.h"
#include "protocols/common/crc.h"
#include "protocols/common/manchester.h"

namespace skyblip::adsb {

constexpr uint8_t kParityOffsetBytes = 11;

inline void seal(uint8_t* frame) {
    const uint32_t parity = wire::crc24_compute(frame, kParityOffsetBytes);
    frame[kParityOffsetBytes] = static_cast<uint8_t>(parity >> 16);
    frame[kParityOffsetBytes + 1] = static_cast<uint8_t>(parity >> 8);
    frame[kParityOffsetBytes + 2] = static_cast<uint8_t>(parity);
}

// INFO: fc 10nov26 1090 sends a 1 as the chips 10, the inverse of manchester_encode's ADS-L table
inline void chips_of(const uint8_t* frame, uint8_t* chips) {
    uint8_t coded[2 * kFrameBytes];
    wire::manchester_encode(frame, kFrameBytes, coded);
    for (uint8_t i = 0; i < kChipBytes; i++) chips[i] = static_cast<uint8_t>(~coded[i + 1]);
}

struct Squitter {
    uint8_t bytes[kFrameBytes]{};
};

inline void set_message_bits(Squitter& frame, uint8_t first, uint8_t last, uint64_t value) {
    for (uint8_t bit = first; bit <= last; bit++) {
        const uint8_t index = static_cast<uint8_t>(32 + bit - 1);
        const uint8_t mask = static_cast<uint8_t>(0x80u >> (index & 7));
        const bool set = (value >> (last - bit)) & 1u;
        frame.bytes[index >> 3] = static_cast<uint8_t>(set ? frame.bytes[index >> 3] | mask
                                                           : frame.bytes[index >> 3] & ~mask);
    }
}

inline Squitter squitter(uint32_t address, uint8_t capability, uint8_t type_code) {
    Squitter frame;
    frame.bytes[0] = static_cast<uint8_t>((17 << 3) | (capability & 7));
    frame.bytes[1] = static_cast<uint8_t>(address >> 16);
    frame.bytes[2] = static_cast<uint8_t>(address >> 8);
    frame.bytes[3] = static_cast<uint8_t>(address);
    set_message_bits(frame, 1, 5, type_code);
    return frame;
}

// INFO: fc 10nov26 the closed form of DO-260B draft A.1.7.2 d, so the decoder's table is checked
inline int longitude_zones_closed_form(double latitude) {
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
inline Cpr cpr_encode(double latitude, double longitude, bool odd, bool surface) {
    const double turn = 360.0;
    const double steps = surface ? 524288.0 : 131072.0;
    const int parity = odd ? 1 : 0;
    const auto modulo = [](double x, double y) { return x - y * std::floor(x / y); };
    const double latitude_zone = turn / (60 - parity);
    const double latitude_cpr =
        std::floor(steps * modulo(latitude, latitude_zone) / latitude_zone + 0.5);
    const double encoded_latitude =
        latitude_zone * (latitude_cpr / steps + std::floor(latitude / latitude_zone));
    const int zones = longitude_zones_closed_form(encoded_latitude) - parity;
    const double longitude_zone = zones > 0 ? turn / zones : turn;
    const double longitude_cpr =
        std::floor(steps * modulo(longitude, longitude_zone) / longitude_zone + 0.5);
    Cpr cpr;
    cpr.odd = odd;
    cpr.latitude_cpr = static_cast<uint32_t>(latitude_cpr) & 0x1FFFF;
    cpr.longitude_cpr = static_cast<uint32_t>(longitude_cpr) & 0x1FFFF;
    return cpr;
}

inline void set_cpr(Squitter& frame, const Cpr& cpr) {
    set_message_bits(frame, 22, 22, cpr.odd ? 1 : 0);
    set_message_bits(frame, 23, 39, cpr.latitude_cpr);
    set_message_bits(frame, 40, 56, cpr.longitude_cpr);
}

// INFO: fc 10nov26 a Q-bit altitude counts 25 ft steps from -1000 ft, with the Q bit after 4 bits
inline uint32_t altitude_field(int32_t feet) {
    const uint32_t steps = static_cast<uint32_t>((feet + 1000 + 12) / 25);
    return ((steps >> 4) << 5) | 0x10u | (steps & 0xFu);
}

inline Squitter airborne_position(uint32_t address, uint8_t capability, double latitude,
                                  double longitude, bool odd, int32_t altitude_ft,
                                  bool gnss_height = false) {
    Squitter frame = squitter(address, capability, gnss_height ? 20 : 11);
    set_message_bits(frame, 9, 20, altitude_field(altitude_ft));
    set_cpr(frame, cpr_encode(latitude, longitude, odd, false));
    return frame;
}

inline uint8_t movement_code(int32_t speed_mkt) {
    const MovementBand* band = kMovementBands;
    for (const MovementBand& candidate : kMovementBands)
        if (speed_mkt >= candidate.floor_mkt) band = &candidate;
    if (band->step_mkt == 0) return band->first_code;
    return static_cast<uint8_t>(band->first_code + (speed_mkt - band->floor_mkt) / band->step_mkt);
}

inline Squitter surface_position(uint32_t address, uint8_t capability, double latitude,
                                 double longitude, bool odd, uint8_t movement = 1) {
    Squitter frame = squitter(address, capability, 7);
    set_message_bits(frame, 6, 12, movement);
    set_cpr(frame, cpr_encode(latitude, longitude, odd, true));
    return frame;
}

inline uint64_t signed_field(int32_t value, int32_t step, uint8_t width) {
    const uint64_t sign = value < 0 ? 1u : 0u;
    const uint64_t magnitude = static_cast<uint64_t>(value < 0 ? -int64_t{value} : value);
    const uint64_t top_code = (uint64_t{1} << width) - 1;
    const uint64_t code = (magnitude + static_cast<uint64_t>(step) / 2) / step + 1;
    return (sign << width) | (code < top_code ? code : top_code);
}

inline Squitter ground_velocity(uint32_t address, uint8_t capability, int32_t east_kt,
                                int32_t north_kt, int32_t climb_fpm,
                                int32_t gnss_minus_pressure_ft) {
    Squitter frame = squitter(address, capability, 19);
    set_message_bits(frame, 6, 8, 1);
    set_message_bits(frame, 14, 24, signed_field(east_kt, 1, 10));
    set_message_bits(frame, 25, 35, signed_field(north_kt, 1, 10));
    set_message_bits(frame, 37, 46, signed_field(climb_fpm, 64, 9));
    set_message_bits(frame, 49, 56, signed_field(gnss_minus_pressure_ft, 25, 7));
    return frame;
}

inline Squitter air_velocity(uint32_t address, uint8_t capability, uint16_t heading_c10,
                             int32_t airspeed_kt, int32_t climb_fpm) {
    Squitter frame = squitter(address, capability, 19);
    set_message_bits(frame, 6, 8, 3);
    set_message_bits(frame, 14, 14, 1);
    set_message_bits(frame, 15, 24, heading_c10);
    set_message_bits(frame, 25, 25, 1);
    set_message_bits(frame, 26, 35, static_cast<uint64_t>(airspeed_kt) + 1);
    set_message_bits(frame, 36, 36, 1);
    set_message_bits(frame, 37, 46, signed_field(climb_fpm, 64, 9));
    return frame;
}

inline Squitter identification(uint32_t address, uint8_t capability, uint8_t type_code,
                               uint8_t category, const char* callsign) {
    static constexpr char kCharacters[] =
        "?ABCDEFGHIJKLMNOPQRSTUVWXYZ????? ???????????????0123456789??????";
    Squitter frame = squitter(address, capability, type_code);
    set_message_bits(frame, 6, 8, category);
    const size_t length = std::strlen(callsign);
    for (uint8_t i = 0; i < kCallsignCharacters; i++) {
        const char character = i < length ? callsign[i] : ' ';
        const bool sendable =
            (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
        const uint8_t code = static_cast<uint8_t>(
            std::strchr(kCharacters, sendable ? character : ' ') - kCharacters);
        set_message_bits(frame, static_cast<uint8_t>(9 + 6 * i), static_cast<uint8_t>(14 + 6 * i),
                         code);
    }
    return frame;
}

inline Squitter operational_status(uint32_t address, uint8_t capability, uint8_t version,
                                   uint8_t gva) {
    Squitter frame = squitter(address, capability, 31);
    set_message_bits(frame, 41, 43, version);
    set_message_bits(frame, 49, 50, gva);
    return frame;
}

}

#endif
