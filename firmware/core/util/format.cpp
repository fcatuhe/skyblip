#include "core/util/format.h"

#include <algorithm>

#include "core/util/intmath.h"

namespace skyblip {

int format_string(char* out, const char* text) {
    int written = 0;
    while (text && text[written]) {
        out[written] = text[written];
        written++;
    }
    return written;
}

int format_hex(char* out, uint32_t value, uint8_t digits) {
    for (int i = digits - 1; i >= 0; i--) {
        out[i] = hex_digit(static_cast<uint8_t>(value & 0x0F));
        value >>= 4;
    }
    return digits;
}

int format_unsigned(char* out, uint32_t value, uint8_t minimum_digits, uint8_t fraction_digits) {
    char reversed[12];
    int count = 0;
    do {
        reversed[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value);
    while (count < minimum_digits) reversed[count++] = '0';
    if (fraction_digits) {
        while (count <= fraction_digits) reversed[count++] = '0';
    }
    int written = 0;
    for (int i = count - 1; i >= 0; i--) {
        if (fraction_digits && i + 1 == fraction_digits) out[written++] = '.';
        out[written++] = reversed[i];
    }
    return written;
}

int format_signed(char* out, int32_t value, uint8_t minimum_digits, uint8_t fraction_digits,
                  bool plus_sign) {
    int written = 0;
    if (value < 0) {
        out[written++] = '-';
        value = -value;
    } else if (plus_sign) {
        out[written++] = '+';
    }
    written += format_unsigned(out + written, static_cast<uint32_t>(value), minimum_digits,
                               fraction_digits);
    return written;
}

namespace {
int format_degrees_minutes(char* out, int32_t coordinate_1e7, int degree_digits) {
    uint32_t magnitude = coordinate_1e7 < 0 ? -coordinate_1e7 : coordinate_1e7;
    uint32_t degrees = magnitude / 10000000u;
    uint64_t remainder =
        static_cast<uint64_t>(magnitude) - static_cast<uint64_t>(degrees) * 10000000u;
    uint64_t minutes_e4 = div_round<uint64_t>(remainder * 60u * 10000u, 10000000u);
    uint32_t minutes = static_cast<uint32_t>(minutes_e4 / 10000u);
    uint32_t fraction = static_cast<uint32_t>(minutes_e4 % 10000u);
    int written = 0;
    written += format_unsigned(out + written, degrees, static_cast<uint8_t>(degree_digits));
    written += format_unsigned(out + written, minutes, 2);
    out[written++] = '.';
    written += format_unsigned(out + written, fraction, 4);
    return written;
}
}

int format_seconds_of_day(char* out, uint32_t seconds) {
    const uint32_t second_of_day = seconds % 86400u;
    int written = format_unsigned(out, second_of_day / 3600u, 2);
    written += format_string(out + written, ":");
    written += format_unsigned(out + written, (second_of_day / 60u) % 60u, 2);
    written += format_string(out + written, ":");
    written += format_unsigned(out + written, second_of_day % 60u, 2);
    return written;
}

int format_flight_clock(char* out, uint32_t seconds, bool flight_time_valid) {
    constexpr uint32_t kMinutesCeiling = 99 * 60 + 59;
    if (!flight_time_valid) {
        const int dashes = format_string(out, "-:--");
        out[dashes] = 0;
        return dashes;
    }
    uint32_t minutes = seconds / 60;
    minutes = std::min(minutes, kMinutesCeiling);
    int written = format_unsigned(out, minutes / 60);
    out[written++] = ':';
    written += format_unsigned(out + written, minutes % 60, 2);
    out[written] = 0;
    return written;
}

int format_nmea_latitude(char* out, int32_t latitude_1e7) {
    int written = format_degrees_minutes(out, latitude_1e7, 2);
    out[written++] = ',';
    out[written++] = latitude_1e7 < 0 ? 'S' : 'N';
    return written;
}

int format_nmea_longitude(char* out, int32_t longitude_1e7) {
    int written = format_degrees_minutes(out, longitude_1e7, 3);
    out[written++] = ',';
    out[written++] = longitude_1e7 < 0 ? 'W' : 'E';
    return written;
}

}
