#include "core/util/format.h"

#include <algorithm>

#include "core/util/intmath.h"

namespace skyblip {

int format_string(char* out, const char* s) {
    int n = 0;
    while (s && s[n]) {
        out[n] = s[n];
        n++;
    }
    return n;
}

int format_hex(char* out, uint32_t value, uint8_t digits) {
    for (int i = digits - 1; i >= 0; i--) {
        out[i] = hex_digit(static_cast<uint8_t>(value & 0x0F));
        value >>= 4;
    }
    return digits;
}

int format_uint(char* out, uint32_t value, uint8_t min_digits, uint8_t fraction_digits) {
    char temporary[12];
    int length = 0;
    do {
        temporary[length++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value);
    while (length < min_digits) temporary[length++] = '0';
    if (fraction_digits) {
        while (length <= fraction_digits) temporary[length++] = '0';
    }
    int n = 0;
    for (int i = length - 1; i >= 0; i--) {
        if (fraction_digits && i + 1 == fraction_digits) out[n++] = '.';
        out[n++] = temporary[i];
    }
    return n;
}

int format_int(char* out, int32_t value, uint8_t min_digits, uint8_t fraction_digits,
               bool plus_sign) {
    int n = 0;
    if (value < 0) {
        out[n++] = '-';
        value = -value;
    } else if (plus_sign) {
        out[n++] = '+';
    }
    n += format_uint(out + n, static_cast<uint32_t>(value), min_digits, fraction_digits);
    return n;
}

namespace {
int format_dm(char* out, int32_t coord_1e7, int deg_digits) {
    uint32_t a = coord_1e7 < 0 ? -coord_1e7 : coord_1e7;
    uint32_t deg = a / 10000000u;
    uint64_t rem = static_cast<uint64_t>(a) - static_cast<uint64_t>(deg) * 10000000u;
    uint64_t minutes_e4 = div_round<uint64_t>(rem * 60u * 10000u, 10000000u);
    uint32_t minutes = static_cast<uint32_t>(minutes_e4 / 10000u);
    uint32_t fraction = static_cast<uint32_t>(minutes_e4 % 10000u);
    int n = 0;
    n += format_uint(out + n, deg, static_cast<uint8_t>(deg_digits));
    n += format_uint(out + n, minutes, 2);
    out[n++] = '.';
    n += format_uint(out + n, fraction, 4);
    return n;
}
}

int format_seconds_of_day(char* out, uint32_t seconds) {
    const uint32_t second_of_day = seconds % 86400u;
    int n = format_uint(out, second_of_day / 3600u, 2);
    n += format_string(out + n, ":");
    n += format_uint(out + n, (second_of_day / 60u) % 60u, 2);
    n += format_string(out + n, ":");
    n += format_uint(out + n, second_of_day % 60u, 2);
    return n;
}

int format_flight_clock(char* out, uint32_t seconds, bool flight_time_valid) {
    constexpr uint32_t kMaxMinutes = 99 * 60 + 59;
    if (!flight_time_valid) {
        const int dashes = format_string(out, "-:--");
        out[dashes] = 0;
        return dashes;
    }
    uint32_t minutes = seconds / 60;
    minutes = std::min(minutes, kMaxMinutes);
    int n = format_uint(out, minutes / 60);
    out[n++] = ':';
    n += format_uint(out + n, minutes % 60, 2);
    out[n] = 0;
    return n;
}

int format_nmea_latitude(char* out, int32_t latitude_1e7) {
    int n = format_dm(out, latitude_1e7, 2);
    out[n++] = ',';
    out[n++] = latitude_1e7 < 0 ? 'S' : 'N';
    return n;
}

int format_nmea_longitude(char* out, int32_t longitude_1e7) {
    int n = format_dm(out, longitude_1e7, 3);
    out[n++] = ',';
    out[n++] = longitude_1e7 < 0 ? 'W' : 'E';
    return n;
}

}
