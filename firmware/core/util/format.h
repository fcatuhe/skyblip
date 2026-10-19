// core/util/format.h: tiny, allocation-free number/string formatting into a
#ifndef SKYBLIP_CORE_UTIL_FORMAT_H
#define SKYBLIP_CORE_UTIL_FORMAT_H

#include <cstdint>

namespace skyblip {

inline char hex_digit(uint8_t v) {
    v &= 0x0F;
    return static_cast<char>(v < 10 ? '0' + v : 'A' + v - 10);
}

int format_string(char* out, const char* s);

int format_hex(char* out, uint32_t value, uint8_t digits);

int format_uint(char* out, uint32_t value, uint8_t min_digits = 1, uint8_t fraction_digits = 0);

int format_int(char* out, int32_t value, uint8_t min_digits = 1, uint8_t fraction_digits = 0,
               bool plus_sign = true);

int format_seconds_of_day(char* out, uint32_t seconds);

int format_flight_clock(char* out, uint32_t seconds, bool flight_time_valid);

int format_nmea_lat(char* out, int32_t lat_1e7);
int format_nmea_lon(char* out, int32_t lon_1e7);

}

#endif
