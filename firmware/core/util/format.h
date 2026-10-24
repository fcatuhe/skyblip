// core/util/format.h: tiny, allocation-free number/string formatting into a
#ifndef SKYBLIP_CORE_UTIL_FORMAT_H
#define SKYBLIP_CORE_UTIL_FORMAT_H

#include <cstdint>

namespace skyblip {

inline char hex_digit(uint8_t nibble) {
    nibble &= 0x0F;
    return static_cast<char>(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
}

int format_string(char* out, const char* text);

int format_hex(char* out, uint32_t value, uint8_t digits);

int format_unsigned(char* out, uint32_t value, uint8_t minimum_digits = 1,
                    uint8_t fraction_digits = 0);

int format_signed(char* out, int32_t value, uint8_t minimum_digits = 1, uint8_t fraction_digits = 0,
                  bool plus_sign = true);

int format_seconds_of_day(char* out, uint32_t seconds);

int format_flight_clock(char* out, uint32_t seconds, bool flight_time_valid);

int format_nmea_latitude(char* out, int32_t latitude_1e7);
int format_nmea_longitude(char* out, int32_t longitude_1e7);

}

#endif
