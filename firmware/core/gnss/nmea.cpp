#include "core/gnss/nmea.h"

#include <cstring>

#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::gnss {

namespace {
int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
long parse_long(const char* s, int len) {
    long v = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') break;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}
int d2(const char* s) { return (s[0] - '0') * 10 + (s[1] - '0'); }
bool digits(const char* s, int n) {
    for (int i = 0; i < n; i++)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

constexpr int64_t kMillimetresPerSecPerKnotE3 = 514444;

constexpr int kMinuteDigits = 2;
constexpr int kRmcStampDigits = 6;
constexpr int kLongitudeDegreeDigits = 3;

// INFO: fc 25sep26 long is 32 bits on the nRF52, and ten times this still fits one
constexpr long kScaledReadingCeiling = 100000000;

// Rounded, not truncated: "46.9" is 47 m of geoid separation, not 46.
bool parse_scaled(const char* s, long scale, long& out) {
    if (!s || !s[0]) return false;
    bool neg = false;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    if (*s < '0' || *s > '9') return false;
    long whole = 0;
    while (*s >= '0' && *s <= '9') {
        whole = whole * 10 + (*s++ - '0');
        if (whole > kScaledReadingCeiling / scale) return false;
    }
    long value = whole * scale * 10;
    if (*s == '.') {
        s++;
        long place = scale * 10;
        while (*s >= '0' && *s <= '9' && place > 0) {
            place /= 10;
            value += (*s++ - '0') * place;
        }
    }
    value = (value + 5) / 10;
    out = neg ? -value : value;
    return true;
}

uint32_t to_epoch(int y, int mon, int day, int hh, int mm, int ss) {
    y -= mon <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy = (153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = int64_t{era} * 146097 + doe - 719468;
    const int second_of_day = hh * 3600 + mm * 60 + ss;
    return static_cast<uint32_t>(days * 86400 + second_of_day);
}
}

bool nmea_checksum_ok(const char* line, int len) {
    if (len < 4 || line[0] != '$') return false;
    int star = -1;
    for (int i = 0; i < len; i++)
        if (line[i] == '*') {
            star = i;
            break;
        }
    if (star < 0 || star + 2 >= len) return false;
    uint8_t cs = 0;
    for (int i = 1; i < star; i++) cs ^= static_cast<uint8_t>(line[i]);
    int hi = hexval(line[star + 1]);
    int lo = hexval(line[star + 2]);
    if (hi < 0 || lo < 0) return false;
    return cs == (hi << 4 | lo);
}

int32_t nmea_parse_coord(const char* dm, char hemi) {
    int dot = -1;
    for (int i = 0; dm[i] && i < 16; i++)
        if (dm[i] == '.') {
            dot = i;
            break;
        }
    if (dot < kMinuteDigits) return 0;
    int deg_digits = dot - kMinuteDigits;
    if (deg_digits > kLongitudeDegreeDigits) return 0;
    long deg = parse_long(dm, deg_digits);
    long min_whole = parse_long(dm + deg_digits, kMinuteDigits);
    long frac = 0, scale = 1;
    for (int i = 0; i < 4; i++) {
        char c = dm[dot + 1 + i];
        if (c < '0' || c > '9') break;
        frac = frac * 10 + (c - '0');
        scale *= 10;
    }
    long min_e4 = min_whole * 10000 + (scale ? frac * (10000 / scale) : 0);
    int64_t v = static_cast<int64_t>(deg) * 10000000LL +
                div_round<int64_t>(static_cast<int64_t>(min_e4) * 10000000LL, 600000LL);
    if (hemi == 'S' || hemi == 'W') v = -v;
    return static_cast<int32_t>(v);
}

bool NmeaParser::feed(char c) {
    if (c == '$') {
        pos_ = 0;
        buf_[pos_++] = c;
        return false;
    }
    if (c == '\r' || c == '\n') {
        if (pos_ > 0) {
            bool ok = parse_line(buf_, pos_);
            pos_ = 0;
            return ok;
        }
        return false;
    }
    if (pos_ > 0 && pos_ < static_cast<int>(sizeof(buf_)) - 1) buf_[pos_++] = c;
    return false;
}

bool NmeaParser::parse_line(const char* line, int len) {
    if (!nmea_checksum_ok(line, len)) return false;
    char tmp[100];
    int n = 0;
    for (int i = 0; i < len && line[i] != '*' && n < 99; i++) tmp[n++] = line[i];
    tmp[n] = 0;
    const char* fields[24];
    int nf = 0;
    fields[nf++] = tmp;
    for (int i = 0; i < n && nf < 24; i++) {
        if (tmp[i] == ',') {
            tmp[i] = 0;
            fields[nf++] = tmp + i + 1;
        }
    }
    if (nf < 1) return false;
    const char* tag = fields[0];
    if (strlen(tag) < 6) return false;
    if (memcmp(tag + 3, "RMC", 3) == 0) return apply_rmc(fields, nf);
    if (memcmp(tag + 3, "GGA", 3) == 0) return apply_gga(fields, nf, len);
    if (memcmp(tag + 3, "GSA", 3) == 0) return apply_gsa(fields, nf);
    if (memcmp(tag + 3, "GSV", 3) == 0) return apply_gsv(tag + 1, fields, nf);
    if (memcmp(tag + 3, "TXT", 3) == 0) return apply_txt(line, len);
    if (memcmp(tag + 3, "GLL", 3) == 0 || memcmp(tag + 3, "VTG", 3) == 0) unrequested_++;
    return false;
}

// INFO: fc 13sep26 GSA is asked for to carry VDOP, which GGA has no field for
bool NmeaParser::apply_gsa(const char* f[], int nf) {
    if (nf <= kGsaVdopField) return false;
    last_ = Sentence::Gsa;
    // INFO: fc 18sep26 one GSA per constellation, and one that solved nothing carries no DOP at all
    long pdop_e2 = 0;
    if (!parse_scaled(f[kGsaPdopField], 100, pdop_e2) || pdop_e2 <= 0) return true;
    solution_.fix_mode = static_cast<uint8_t>(parse_long(f[kGsaFixModeField], 1));
    long vdop_e2 = 0;
    solution_.vdop_e2 =
        parse_scaled(f[kGsaVdopField], 100, vdop_e2) && vdop_e2 > 0 && vdop_e2 <= 0xFFFF
            ? static_cast<uint16_t>(vdop_e2)
            : 0;

    const System system =
        nf > kGsaSystemField
            ? system_of_gsa_id(static_cast<uint8_t>(parse_long(f[kGsaSystemField], 1)))
            : System::Unknown;
    for (int slot = 0; slot < kGsaSatSlots && kGsaFirstSatField + slot < nf; slot++)
        sky_.solving(system, static_cast<uint8_t>(parse_long(f[kGsaFirstSatField + slot], 3)));
    return true;
}

// INFO: fc 18sep26 QZSS rides on the GP talker and is told apart by its satellite id alone
bool NmeaParser::apply_gsv(const char* talker, const char* f[], int nf) {
    if (nf <= kGsvFirstSatField) return false;
    last_ = Sentence::Gsv;
    const System talker_system = system_of_talker(talker, 0);
    if (parse_long(f[2], 2) == 1) {
        sky_.open(talker_system);
        if (talker_system == System::Gps) sky_.open(System::Qzss);
    }
    for (int at = kGsvFirstSatField; at + kGsvFieldsPerSat - 1 < nf; at += kGsvFieldsPerSat) {
        SatelliteView sat;
        sat.id = static_cast<uint8_t>(parse_long(f[at], 3));
        if (sat.id == 0) continue;
        sat.system = system_of_talker(talker, sat.id);
        sat.elevation_deg = static_cast<uint8_t>(parse_long(f[at + 1], 2));
        sat.azimuth_deg = static_cast<uint16_t>(parse_long(f[at + 2], 3));
        sat.cn0_dbhz = static_cast<uint8_t>(parse_long(f[at + 3], 2));
        sky_.add(sat);
    }
    return true;
}

// $GPTXT,01,01,02,SW=URANUS5,V5.1.0.0 - the CASIC firmware banner, and the only
// evidence that the part answering us speaks $PCAS at all. The version text is
// everything after "SW=", which is where SoftRF reads it from too.
// The version text carries commas of its own ("SW=URANUS5,V5.1.0.0"), so it is
// read off the raw line rather than out of the split fields: it is one string,
// not three, and SoftRF takes the same run of characters up to the checksum.
bool NmeaParser::apply_txt(const char* line, int len) {
    int at = 0;
    for (int commas = 0; at < len && commas < 4; at++)
        if (line[at] == ',') commas++;
    // Only the SW= banner. The part also emits $GPTXT for antenna status and
    // start-up notices, and a version field that is sometimes an antenna warning
    // is worse than no version field.
    if (len - at < 3 || line[at] != 'S' || line[at + 1] != 'W' || line[at + 2] != '=') return false;
    at += 3;
    int n = 0;
    for (; at < len && line[at] != '*' && n < kVersionCap - 1; at++, n++) version_[n] = line[at];
    version_[n] = 0;
    last_ = Sentence::Txt;
    // Not a solution: `updates` counts fixes, and the verification window that
    // reads it must not be satisfied by the receiver introducing itself.
    return n > 0;
}

// INFO: fc 03aug26 RMC fields 1 and 9 are UTC as the receiver already resolved
// it, so the GPS-UTC leap second count never enters this arithmetic. The
// moshe-braner fork queries and persists the count and reboots when it changes
// (.../src/driver/GNSS.cpp:1610-1679) because it reads u-blox NAV-TIMEGPS, which
// reports GPS time; that correction has no reader here and adding one would be a
// second, wrong, clock. This device's only failure mode in that direction is a
// receiver whose almanac is stale, and the date sanity below is what catches it.
bool NmeaParser::apply_rmc(const char* f[], int nf) {
    if (nf < 10) return false;
    last_ = Sentence::Rmc;
    bool valid = f[2][0] == 'A';
    solution_.is_fix = valid;
    solution_.utc_valid = false;
    if (digits(f[1], kRmcStampDigits) && digits(f[9], kRmcStampDigits)) {
        int hh = d2(f[1]), mm = d2(f[1] + 2), ss = d2(f[1] + 4);
        int day = d2(f[9]), mon = d2(f[9] + 2), yy = d2(f[9] + 4);
        // The MTK 1980 lie and its neighbours: a two-digit year of 70 or more is
        // a receiver that has not decoded the almanac, not a date.
        const bool date_sane = yy < kMaxTwoDigitYear && mon >= 1 && mon <= 12 && day >= 1 &&
                               day <= 31 && hh < 24 && mm < 60 && ss < 62;
        if (date_sane) {
            solution_.utc = to_epoch(2000 + yy, mon, day, hh, mm, ss);
            solution_.utc_valid = true;
        }
    }
    if (valid) {
        if (f[3][0]) solution_.lat_1e7 = nmea_parse_coord(f[3], f[4][0]);
        if (f[5][0]) solution_.lon_1e7 = nmea_parse_coord(f[5], f[6][0]);
        long knots_e2 = 0;
        if (parse_scaled(f[7], 100, knots_e2))
            solution_.speed_mm_s = static_cast<int32_t>(
                div_round<int64_t>(knots_e2 * kMillimetresPerSecPerKnotE3, 100000));
        long track_cdeg = 0;
        if (parse_scaled(f[8], 100, track_cdeg))
            solution_.track_cdeg =
                static_cast<int32_t>(((track_cdeg % kCentiDegreesPerTurn) + kCentiDegreesPerTurn) %
                                     kCentiDegreesPerTurn);
    }
    solution_.updates++;
    return true;
}

bool NmeaParser::apply_gga(const char* f[], int nf, int len) {
    // A sentence that stopped early still checksums: length is the only thing
    // that catches it, which is why moshe-braner measures it.
    if (nf < 10 || len < kMinGgaLength) return false;
    last_ = Sentence::Gga;
    sky_.clear_solution();
    solution_.fix_quality = static_cast<uint8_t>(parse_long(f[6], 2));
    solution_.sats = static_cast<uint8_t>(parse_long(f[7], 2));

    long hdop_e2 = 0;
    solution_.hdop_e2 = parse_scaled(f[8], 100, hdop_e2) && hdop_e2 > 0 && hdop_e2 <= 0xFFFF
                            ? static_cast<uint16_t>(hdop_e2)
                            : 0;

    long separation_mm = 0;
    solution_.geoid_separation_measured = nf > 11 && parse_scaled(f[11], 1000, separation_mm) &&
                                          separation_mm != 0 && separation_mm > -200000 &&
                                          separation_mm < 200000;
    solution_.geoid_separation_mm = solution_.geoid_separation_measured
                                        ? static_cast<int32_t>(separation_mm)
                                        : kDefaultGeoidSeparationMm;

    long msl_mm = 0;
    solution_.alt_msl_valid = parse_scaled(f[9], 1000, msl_mm);
    solution_.alt_hae_valid = solution_.alt_msl_valid;
    if (solution_.alt_msl_valid) {
        solution_.alt_msl_mm = static_cast<int32_t>(msl_mm);
        solution_.alt_mm = solution_.alt_msl_mm + solution_.geoid_separation_mm;
    }

    solution_.updates++;
    return true;
}

}
