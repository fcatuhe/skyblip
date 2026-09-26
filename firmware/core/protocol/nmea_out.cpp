#include "core/protocol/nmea_out.h"

#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/units/units.h"
#include "core/util/format.h"
#include "core/util/intmath.h"

namespace skyblip::protocol {

int nmea_finish(char* s, int body_len) {
    uint8_t cs = 0;
    for (int i = 1; i < body_len; i++) cs ^= static_cast<uint8_t>(s[i]);
    int n = body_len;
    s[n++] = '*';
    s[n++] = hex_digit(static_cast<uint8_t>(cs >> 4));
    s[n++] = hex_digit(static_cast<uint8_t>(cs & 0x0F));
    s[n++] = '\r';
    s[n++] = '\n';
    s[n] = 0;
    return n;
}

uint8_t adsl_cat_to_alptas(uint8_t adsl_cat) {
    static const uint8_t kMap[18] = {0x0, 0x8, 0x8, 0x3, 0x1, 0xB, 0xC, 0x7, 0x4,
                                     0x8, 0x3, 0xD, 0xD, 0xD, 0x0, 0x0, 0x0, 0x0};
    return adsl_cat < 18 ? kMap[adsl_cat] : 0;
}

// INFO: fc 03aug26 SkyDemon draws nothing at IDType 0, so ICAO is 1 and all else 2 (README.md)
uint8_t addr_table_to_idtype(uint8_t addr_table) { return addr_table == 0x05 ? 1 : 2; }

bool relative_ned(const model::OwnState& own, const model::AircraftObs& t, int32_t& north_m,
                  int32_t& east_m, int32_t& up_m) {
    if (!own.fix_valid || !t.position_valid) return false;
    int64_t dlat = static_cast<int64_t>(t.lat_1e7) - own.lat_1e7;
    int64_t dlon = wrapped_lon_1e7(static_cast<int64_t>(t.lon_1e7) - own.lon_1e7);
    north_m = static_cast<int32_t>(div_round<int64_t>(dlat * 11132, 1000000));
    int16_t ang = static_cast<int16_t>((static_cast<int64_t>(own.lat_1e7) * 65536) / 3600000000LL);
    int64_t coslat = icos(ang);
    int64_t east_um = div_round<int64_t>(dlon * 11132 * coslat, 16384);
    east_m = static_cast<int32_t>(div_round<int64_t>(east_um, 1000000));
    up_m = t.alt_valid ? t.alt_m - to_metres(Millimetres(own.alt_mm)).v : 0;
    return true;
}

int format_pflaa(char* out, size_t cap, const model::OwnState& own, const model::AircraftObs& t,
                 uint8_t alarm_level, const char* callsign) {
    (void)cap;
    int32_t n_m, e_m, u_m;
    if (!relative_ned(own, t, n_m, e_m, u_m)) return 0;
    int n = 0;
    n += fmt_string(out + n, "$PFLAA,");
    n += fmt_uint(out + n, alarm_level);
    out[n++] = ',';
    n += fmt_int(out + n, n_m, 1, 0, true);
    out[n++] = ',';
    n += fmt_int(out + n, e_m, 1, 0, true);
    out[n++] = ',';
    n += fmt_int(out + n, u_m, 1, 0, true);
    out[n++] = ',';
    n += fmt_uint(out + n, addr_table_to_idtype(t.addr_table));
    out[n++] = ',';
    n += fmt_hex(out + n, t.addr, 6);
    if (callsign != nullptr && callsign[0] != 0) {
        out[n++] = '!';
        n += fmt_string(out + n, callsign);
    }
    out[n++] = ',';
    if (t.flight_state != 1) {
        uint16_t deg = to_degrees(Cordic9(t.track_c9)).v;
        n += fmt_uint(out + n, deg);
    }
    out[n++] = ',';
    out[n++] = ',';
    if (t.speed_valid) n += fmt_uint(out + n, (static_cast<uint32_t>(t.speed_q) + 2) / 4);
    out[n++] = ',';
    if (t.climb_valid) {
        int32_t climb_dm = (static_cast<int32_t>(t.climb_e8) * 10 + 4) / 8;
        n += fmt_int(out + n, climb_dm, 1, 1, true);
    } else {
        out[n++] = '0';
    }
    out[n++] = ',';
    out[n++] = hex_digit(adsl_cat_to_alptas(t.aircraft_cat));
    return nmea_finish(out, n);
}

// INFO: fc 19sep26 Unknown is not a claim of being on the ground, so only OnGround answers 1.
uint8_t pflau_gps(const model::OwnState& own) {
    if (!own.fix_valid) return 0;
    return flight::state_from(own.flight_state) == flight::FlightState::OnGround ? 1 : 2;
}

int format_pflau(char* out, size_t cap, const model::OwnState& own, bool transmitting,
                 int n_targets, const model::AircraftObs* threat, uint8_t alarm_level,
                 int16_t rel_bearing_deg, int32_t rel_alt_m, int32_t rel_dist_m) {
    (void)cap;
    int n = 0;
    n += fmt_string(out + n, "$PFLAU,");
    n += fmt_uint(out + n, static_cast<uint32_t>(n_targets));
    out[n++] = ',';
    out[n++] = transmitting ? '1' : '0';
    out[n++] = ',';
    out[n++] = static_cast<char>('0' + pflau_gps(own));
    out[n++] = ',';
    out[n++] = '1';
    out[n++] = ',';
    n += fmt_uint(out + n, alarm_level);
    out[n++] = ',';
    if (threat) {
        n += fmt_int(out + n, static_cast<int32_t>(rel_bearing_deg), 1, 0, true);
        out[n++] = ',';
        n += fmt_uint(out + n, alarm_level ? 2u : 0u);
        out[n++] = ',';
        n += fmt_int(out + n, rel_alt_m, 1, 0, true);
        out[n++] = ',';
        n += fmt_uint(out + n, static_cast<uint32_t>(rel_dist_m < 0 ? 0 : rel_dist_m));
        out[n++] = ',';
        n += fmt_hex(out + n, threat->addr, 6);
    } else {
        out[n++] = ',';
        out[n++] = '0';
        out[n++] = ',';
        out[n++] = ',';
        out[n++] = ',';
    }
    return nmea_finish(out, n);
}

int format_pgrmz(char* out, size_t cap, int32_t alt_ft, bool fix_valid) {
    (void)cap;
    int n = 0;
    n += fmt_string(out + n, "$PGRMZ,");
    n += fmt_int(out + n, alt_ft, 1, 0, true);
    n += fmt_string(out + n, ",F,");
    out[n++] = fix_valid ? '3' : '1';
    return nmea_finish(out, n);
}

namespace {

int32_t clamp_i32(int32_t v, int32_t low, int32_t high) {
    return v < low ? low : (v > high ? high : v);
}

}  // namespace

// Field order, from the LK8000 definition and the three senders that agree on
// it (SoftRF lyusupov src/protocol/data/NMEA.cpp:246-256, SoftRF MB
// src/driver/Baro.cpp:404-415, GXAirCom src/main.cpp:4370-4385):
//   1 raw pressure, pascals (a hundredth of a millibar)
//   2 altitude, metres on the 1013.25 datum - a consumer that got field 1
//     recomputes this for itself and ignores what is here
//   3 vertical speed, centimetres per second
//   4 temperature, degrees Celsius
//   5 battery: volts below 1000, percent plus 1000 at or above it
int format_lk8ex1(char* out, size_t cap, const Lk8Ex1& v) {
    (void)cap;
    const uint32_t pressure =
        v.pressure_valid ? (v.pressure_pa > kLk8MaxPressurePa ? kLk8MaxPressurePa : v.pressure_pa)
                         : kLk8NoPressurePa;
    const int32_t alt =
        v.alt_valid ? clamp_i32(v.alt_m, kLk8MinAltitudeM, kLk8MaxAltitudeM) : kLk8NoAltitudeM;
    const int32_t vario =
        v.vario_valid ? clamp_i32(v.vario_cm_s, kLk8MinVarioCmS, kLk8MaxVarioCmS) : kLk8NoVarioCmS;
    const int32_t temperature =
        v.temperature_valid ? clamp_i32(v.temperature_c, kLk8MinTemperatureC, kLk8MaxTemperatureC)
                            : kLk8NoTemperatureC;
    const uint32_t percent = v.battery_percent > 100 ? 100u : v.battery_percent;
    const uint32_t battery = v.battery_valid ? kLk8BatteryPercentBase + percent : kLk8NoBattery;

    int n = 0;
    n += fmt_string(out + n, "$LK8EX1,");
    n += fmt_uint(out + n, pressure);
    out[n++] = ',';
    n += fmt_int(out + n, alt, 1, 0, true);
    out[n++] = ',';
    n += fmt_int(out + n, vario, 1, 0, true);
    out[n++] = ',';
    n += fmt_int(out + n, temperature, 1, 0, true);
    out[n++] = ',';
    n += fmt_uint(out + n, battery);
    return nmea_finish(out, n);
}

namespace {

// The inverse of the epoch core/gnss/nmea.cpp builds from a $GPRMC date and
// time: own.utc keeps no calendar fields of its own, so writing one back out
// means undoing the conversion. Howard Hinnant's civil_from_days, exact over
// the whole range a uint32_t epoch can name.
void civil_from_epoch(uint32_t utc, int& year, int& month, int& day, int& hh, int& mm, int& ss) {
    const uint32_t sod = utc % 86400u;
    hh = static_cast<int>(sod / 3600u);
    mm = static_cast<int>((sod / 60u) % 60u);
    ss = static_cast<int>(sod % 60u);
    const int64_t z = static_cast<int64_t>(utc / 86400u) + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    month = static_cast<int>(mp) + (mp < 10 ? 3 : -9);
    year = static_cast<int>(y + (month <= 2 ? 1 : 0));
}

int put_hhmmss(char* out, uint32_t utc) {
    const uint32_t sod = utc % 86400u;
    int n = 0;
    n += fmt_uint(out + n, sod / 3600u, 2);
    n += fmt_uint(out + n, (sod / 60u) % 60u, 2);
    n += fmt_uint(out + n, sod % 60u, 2);
    return n;
}

}  // namespace

int format_gprmc(char* out, size_t cap, const model::OwnState& own) {
    (void)cap;
    if (!own.fix_valid || !own.utc_valid) return 0;
    int year, month, day, hh, mm, ss;
    civil_from_epoch(own.utc, year, month, day, hh, mm, ss);
    int n = 0;
    n += fmt_string(out + n, "$GPRMC,");
    n += fmt_uint(out + n, static_cast<uint32_t>(hh), 2);
    n += fmt_uint(out + n, static_cast<uint32_t>(mm), 2);
    n += fmt_uint(out + n, static_cast<uint32_t>(ss), 2);
    n += fmt_string(out + n, ",A,");
    n += fmt_nmea_lat(out + n, own.lat_1e7);
    out[n++] = ',';
    n += fmt_nmea_lon(out + n, own.lon_1e7);
    out[n++] = ',';
    // Ground speed is knots on this sentence, kept to one decimal.
    const uint32_t knots_e1 = static_cast<uint32_t>(
        div_round<int64_t>(static_cast<int64_t>(own.speed_mm_s) * 194384, 10000000));
    n += fmt_uint(out + n, knots_e1, 1, 1);
    out[n++] = ',';
    n += fmt_uint(out + n, to_degrees(CentiDegrees(own.track_cdeg)).v, 1);
    out[n++] = ',';
    n += fmt_uint(out + n, static_cast<uint32_t>(day), 2);
    n += fmt_uint(out + n, static_cast<uint32_t>(month), 2);
    n += fmt_uint(out + n, static_cast<uint32_t>(year % 100), 2);
    n += fmt_string(out + n, ",,,A");
    return nmea_finish(out, n);
}

int format_gpgga(char* out, size_t cap, const model::OwnState& own) {
    (void)cap;
    if (!own.fix_valid || !own.utc_valid) return 0;
    int n = 0;
    n += fmt_string(out + n, "$GPGGA,");
    n += put_hhmmss(out + n, own.utc);
    out[n++] = ',';
    n += fmt_nmea_lat(out + n, own.lat_1e7);
    out[n++] = ',';
    n += fmt_nmea_lon(out + n, own.lon_1e7);
    n += fmt_string(out + n, ",1,");
    n += fmt_uint(out + n, own.sats, 2);
    out[n++] = ',';
    n += fmt_uint(out + n, own.hdop_e2, 3, 2);
    out[n++] = ',';
    n += fmt_int(out + n, to_metres(Millimetres(own.alt_msl_mm)).v, 1, 0, true);
    n += fmt_string(out + n, ",M,");
    n += fmt_int(out + n, to_metres(Millimetres(own.alt_mm - own.alt_msl_mm)).v, 1, 0, true);
    n += fmt_string(out + n, ",M,,");
    return nmea_finish(out, n);
}

}
