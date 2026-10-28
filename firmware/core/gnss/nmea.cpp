#include "core/gnss/nmea.h"

#include <cstring>

#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::gnss {

namespace {
int hex_value(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    return -1;
}
long parse_long(const char* text, int bytes) {
    long value = 0;
    for (int i = 0; i < bytes; i++) {
        if (text[i] < '0' || text[i] > '9') break;
        value = value * 10 + (text[i] - '0');
    }
    return value;
}
int two_digits(const char* text) { return (text[0] - '0') * 10 + (text[1] - '0'); }
bool all_digits(const char* text, int count) {
    for (int i = 0; i < count; i++)
        if (text[i] < '0' || text[i] > '9') return false;
    return true;
}

constexpr int64_t kMillimetresPerSecondPerKnotE3 = 514444;
constexpr int kRmcStampDigits = 6;

// INFO: fc 23sep26 no aircraft flies past COCOM's 1000 kn, the Karman line, or 1 km under the sea
constexpr int64_t kSpeedCeilingMmS = 514444;
constexpr int64_t kAltitudeCeilingM = 100000;
constexpr int64_t kAltitudeFloorM = -1000;

// Rounded, not truncated: "46.9" is 47 m of geoid separation, not 46.
bool parse_scaled(const char* text, int64_t scale, int64_t& out) {
    if (!text || !text[0]) return false;
    bool negative = false;
    if (*text == '-' || *text == '+') negative = *text++ == '-';
    if (*text < '0' || *text > '9') return false;
    int64_t whole = 0;
    while (*text >= '0' && *text <= '9') {
        whole = whole * 10 + (*text++ - '0');
        if (whole > INT32_MAX) return false;
    }
    int64_t value = whole * scale * 10;
    if (*text == '.') {
        text++;
        int64_t place = scale * 10;
        while (*text >= '0' && *text <= '9' && place > 0) {
            place /= 10;
            value += (*text++ - '0') * place;
        }
    }
    value = (value + 5) / 10;
    out = negative ? -value : value;
    return true;
}

uint32_t to_epoch(int year, int month, int day, int hours, int minutes, int seconds) {
    year -= month <= 2;
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    unsigned day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    int64_t days = static_cast<int64_t>(era) * 146097 + day_of_era - 719468;
    const int second_of_day = hours * 3600 + minutes * 60 + seconds;
    return static_cast<uint32_t>(days * 86400 + second_of_day);
}
}

bool nmea_checksum_ok(const char* line, int bytes) {
    if (bytes < 4 || line[0] != '$') return false;
    int star = -1;
    for (int i = 0; i < bytes; i++)
        if (line[i] == '*') {
            star = i;
            break;
        }
    if (star < 0 || star + 2 >= bytes) return false;
    uint8_t checksum = 0;
    for (int i = 1; i < star; i++) checksum ^= static_cast<uint8_t>(line[i]);
    int high = hex_value(line[star + 1]);
    int low = hex_value(line[star + 2]);
    if (high < 0 || low < 0) return false;
    return checksum == (high << 4 | low);
}

bool nmea_parse_coordinate(const char* degrees_minutes, char hemisphere, int32_t& out_1e7) {
    const bool latitude = hemisphere == 'N' || hemisphere == 'S';
    if (!latitude && hemisphere != 'E' && hemisphere != 'W') return false;
    int dot = -1;
    for (int i = 0; degrees_minutes[i] && i < 16; i++)
        if (degrees_minutes[i] == '.') {
            dot = i;
            break;
        }
    if (dot < 2 || dot > 5) return false;
    int degree_digits = dot - 2;
    long degrees = parse_long(degrees_minutes, degree_digits);
    long minutes_whole = parse_long(degrees_minutes + degree_digits, 2);
    if (minutes_whole >= 60) return false;
    long fraction = 0, scale = 1;
    for (int i = 0; i < 4; i++) {
        char digit = degrees_minutes[dot + 1 + i];
        if (digit < '0' || digit > '9') break;
        fraction = fraction * 10 + (digit - '0');
        scale *= 10;
    }
    long minutes_e4 = minutes_whole * 10000 + (scale ? fraction * (10000 / scale) : 0);
    int64_t coordinate_1e7 =
        static_cast<int64_t>(degrees) * 10000000LL +
        divide_rounded<int64_t>(static_cast<int64_t>(minutes_e4) * 10000000LL, 600000LL);
    if (coordinate_1e7 > (latitude ? 900000000 : 1800000000)) return false;
    out_1e7 = static_cast<int32_t>(hemisphere == 'S' || hemisphere == 'W' ? -coordinate_1e7
                                                                          : coordinate_1e7);
    return true;
}

bool NmeaParser::feed(char character) {
    if (character == '$') {
        line_bytes_ = 0;
        line_[line_bytes_++] = character;
        return false;
    }
    if (character == '\r' || character == '\n') {
        if (line_bytes_ > 0) {
            bool parsed = parse_line(line_, line_bytes_);
            line_bytes_ = 0;
            return parsed;
        }
        return false;
    }
    if (line_bytes_ > 0 && line_bytes_ < static_cast<int>(sizeof(line_)) - 1)
        line_[line_bytes_++] = character;
    return false;
}

bool NmeaParser::parse_line(const char* line, int bytes) {
    if (!nmea_checksum_ok(line, bytes)) return false;
    char line_copy[100] = {};
    int copied = 0;
    for (int i = 0; i < bytes && line[i] != '*' && copied < 99; i++) line_copy[copied++] = line[i];
    line_copy[copied] = 0;
    const char* fields[24];
    int field_count = 0;
    fields[field_count++] = line_copy;
    for (int i = 0; i < copied && field_count < 24; i++) {
        if (line_copy[i] == ',') {
            line_copy[i] = 0;
            fields[field_count++] = line_copy + i + 1;
        }
    }
    if (field_count < 1) return false;
    const char* tag = fields[0];
    if (strlen(tag) < 6) return false;
    if (memcmp(tag + 3, "RMC", 3) == 0) return apply_rmc(fields, field_count);
    if (memcmp(tag + 3, "GGA", 3) == 0) return apply_gga(fields, field_count, bytes);
    if (memcmp(tag + 3, "GSA", 3) == 0) return apply_gsa(fields, field_count);
    if (memcmp(tag + 3, "GSV", 3) == 0) return apply_gsv(tag + 1, fields, field_count);
    if (memcmp(tag + 3, "TXT", 3) == 0) return apply_txt(line, bytes);
    if (memcmp(tag + 3, "GLL", 3) == 0 || memcmp(tag + 3, "VTG", 3) == 0) unrequested_++;
    return false;
}

// INFO: fc 13sep26 GSA is asked for to carry VDOP, which GGA has no field for
bool NmeaParser::apply_gsa(const char* fields[], int field_count) {
    if (field_count <= kGsaVdopField) return false;
    last_sentence_ = Sentence::Gsa;
    // INFO: fc 18sep26 one GSA per constellation, and one that solved nothing carries no DOP at all
    int64_t pdop_e2 = 0;
    if (!parse_scaled(fields[kGsaPdopField], 100, pdop_e2) || pdop_e2 <= 0) return true;
    solution_.fix_mode = static_cast<uint8_t>(parse_long(fields[kGsaFixModeField], 1));
    int64_t vdop_e2 = 0;
    solution_.vdop_e2 =
        parse_scaled(fields[kGsaVdopField], 100, vdop_e2) && vdop_e2 > 0 && vdop_e2 <= 0xFFFF
            ? static_cast<uint16_t>(vdop_e2)
            : 0;

    const System system =
        field_count > kGsaSystemField
            ? system_of_gsa_id(static_cast<uint8_t>(parse_long(fields[kGsaSystemField], 1)))
            : System::Unknown;
    for (int slot = 0; slot < kGsaSatelliteSlots && kGsaFirstSatelliteField + slot < field_count;
         slot++)
        sky_.solving(system,
                     static_cast<uint8_t>(parse_long(fields[kGsaFirstSatelliteField + slot], 3)));
    return true;
}

// INFO: fc 18sep26 QZSS rides on the GP talker and is told apart by its satellite id alone
bool NmeaParser::apply_gsv(const char* talker, const char* fields[], int field_count) {
    if (field_count <= kGsvFirstSatelliteField) return false;
    last_sentence_ = Sentence::Gsv;
    const System talker_system = system_of_talker(talker, 0);
    if (parse_long(fields[2], 2) == 1) {
        sky_.open(talker_system);
        if (talker_system == System::Gps) sky_.open(System::Qzss);
    }
    for (int at = kGsvFirstSatelliteField; at + kGsvFieldsPerSatellite - 1 < field_count;
         at += kGsvFieldsPerSatellite) {
        SatelliteView satellite;
        satellite.id = static_cast<uint8_t>(parse_long(fields[at], 3));
        if (satellite.id == 0) continue;
        satellite.system = system_of_talker(talker, satellite.id);
        satellite.elevation_deg = static_cast<uint8_t>(parse_long(fields[at + 1], 2));
        satellite.azimuth_deg = static_cast<uint16_t>(parse_long(fields[at + 2], 3));
        satellite.cn0_dbhz = static_cast<uint8_t>(parse_long(fields[at + 3], 2));
        sky_.add(satellite);
    }
    return true;
}

// $GPTXT,01,01,02,SW=URANUS5,V5.1.0.0 - the CASIC firmware banner, and the only
// evidence that the part answering us speaks $PCAS at all. The version text is
// everything after "SW=", which is where SoftRF reads it from too.
// The version text carries commas of its own ("SW=URANUS5,V5.1.0.0"), so it is
// read off the raw line rather than out of the split fields: it is one string,
// not three, and SoftRF takes the same run of characters up to the checksum.
bool NmeaParser::apply_txt(const char* line, int bytes) {
    int at = 0;
    for (int commas = 0; at < bytes && commas < 4; at++)
        if (line[at] == ',') commas++;
    // Only the SW= banner. The part also emits $GPTXT for antenna status and
    // start-up notices, and a version field that is sometimes an antenna warning
    // is worse than no version field.
    if (bytes - at < 3 || line[at] != 'S' || line[at + 1] != 'W' || line[at + 2] != '=')
        return false;
    at += 3;
    char version[kVersionBytes]{};
    int written = 0;
    for (; at < bytes && line[at] != '*' && written < kVersionBytes - 1; at++)
        if (line[at] >= ' ' && line[at] <= '~') version[written++] = line[at];
    last_sentence_ = Sentence::Txt;
    if (written == 0) return false;
    std::memcpy(firmware_version_, version, sizeof(version));
    // Not a solution: `updates` counts fixes, and the verification window that
    // reads it must not be satisfied by the receiver introducing itself.
    return true;
}

// INFO: fc 03aug26 RMC fields 1 and 9 are UTC as the receiver already resolved
// it, so the GPS-UTC leap second count never enters this arithmetic. The
// moshe-braner fork queries and persists the count and reboots when it changes
// (.../src/driver/GNSS.cpp:1610-1679) because it reads u-blox NAV-TIMEGPS, which
// reports GPS time; that correction has no reader here and adding one would be a
// second, wrong, clock. This device's only failure mode in that direction is a
// receiver whose almanac is stale, and the date sanity below is what catches it.
bool NmeaParser::apply_rmc(const char* fields[], int field_count) {
    if (field_count < 10) return false;
    last_sentence_ = Sentence::Rmc;
    int32_t latitude_1e7 = 0;
    int32_t longitude_1e7 = 0;
    solution_.fix_valid = fields[2][0] == 'A' &&
                          nmea_parse_coordinate(fields[3], fields[4][0], latitude_1e7) &&
                          nmea_parse_coordinate(fields[5], fields[6][0], longitude_1e7);
    solution_.utc_valid = false;
    if (all_digits(fields[1], kRmcStampDigits) && all_digits(fields[9], kRmcStampDigits)) {
        int hours = two_digits(fields[1]), minutes = two_digits(fields[1] + 2),
            seconds = two_digits(fields[1] + 4);
        int day = two_digits(fields[9]), month = two_digits(fields[9] + 2),
            year = two_digits(fields[9] + 4);
        // The MTK 1980 lie and its neighbours: a two-digit year of 70 or more is
        // a receiver that has not decoded the almanac, not a date.
        const bool date_sane = year < kTwoDigitYearCeiling && month >= 1 && month <= 12 &&
                               day >= 1 && day <= 31 && hours < 24 && minutes < 60 && seconds < 62;
        if (date_sane) {
            solution_.utc_s = to_epoch(2000 + year, month, day, hours, minutes, seconds);
            solution_.utc_valid = true;
        }
    }
    if (solution_.fix_valid) {
        solution_.latitude_1e7 = latitude_1e7;
        solution_.longitude_1e7 = longitude_1e7;
        int64_t knots_e2 = 0;
        int64_t speed_mm_s = -1;
        if (parse_scaled(fields[7], 100, knots_e2))
            speed_mm_s = divide_rounded<int64_t>(knots_e2 * kMillimetresPerSecondPerKnotE3, 100000);
        solution_.speed_mm_s = speed_mm_s >= 0 && speed_mm_s <= kSpeedCeilingMmS
                                   ? static_cast<int32_t>(speed_mm_s)
                                   : 0;
        int64_t track_cdeg = 0;
        if (parse_scaled(fields[8], 100, track_cdeg))
            solution_.track_cdeg =
                static_cast<int32_t>(((track_cdeg % kCentiDegreesPerTurn) + kCentiDegreesPerTurn) %
                                     kCentiDegreesPerTurn);
    }
    solution_.sentences++;
    return true;
}

bool NmeaParser::apply_gga(const char* fields[], int field_count, int bytes) {
    // A sentence that stopped early still checksums: length is the only thing
    // that catches it, which is why moshe-braner measures it.
    if (field_count < 10 || bytes < kGgaLengthFloorBytes) return false;
    last_sentence_ = Sentence::Gga;
    sky_.clear_solution();
    solution_.fix_quality = static_cast<uint8_t>(parse_long(fields[6], 2));
    solution_.satellites = static_cast<uint8_t>(parse_long(fields[7], 2));

    int64_t hdop_e2 = 0;
    solution_.hdop_e2 = parse_scaled(fields[8], 100, hdop_e2) && hdop_e2 > 0 && hdop_e2 <= 0xFFFF
                            ? static_cast<uint16_t>(hdop_e2)
                            : 0;

    int64_t separation_mm = 0;
    solution_.geoid_measured = field_count > 11 && parse_scaled(fields[11], 1000, separation_mm) &&
                               separation_mm != 0 && separation_mm > -200000 &&
                               separation_mm < 200000;
    solution_.geoid_separation_mm =
        solution_.geoid_measured ? static_cast<int32_t>(separation_mm) : kDefaultGeoidSeparationMm;

    int64_t altitude_msl_mm = 0;
    solution_.altitude_msl_valid = parse_scaled(fields[9], 1000, altitude_msl_mm) &&
                                   altitude_msl_mm >= kAltitudeFloorM * 1000 &&
                                   altitude_msl_mm <= kAltitudeCeilingM * 1000;
    solution_.altitude_hae_valid = solution_.altitude_msl_valid;
    if (solution_.altitude_msl_valid) {
        solution_.altitude_msl_mm = static_cast<int32_t>(altitude_msl_mm);
        solution_.altitude_hae_mm = solution_.altitude_msl_mm + solution_.geoid_separation_mm;
    }

    solution_.sentences++;
    return true;
}

}
