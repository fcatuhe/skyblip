#include "core/protocol/nmea_out.h"

#include <algorithm>

#include "core/flight/extrapolate.h"
#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/units/units.h"
#include "core/util/format.h"
#include "core/util/intmath.h"

namespace skyblip::protocol {

int nmea_finish(char* s, int body_bytes) {
    uint8_t cs = 0;
    for (int i = 1; i < body_bytes; i++) cs ^= static_cast<uint8_t>(s[i]);
    int n = body_bytes;
    s[n++] = '*';
    s[n++] = hex_digit(static_cast<uint8_t>(cs >> 4));
    s[n++] = hex_digit(static_cast<uint8_t>(cs & 0x0F));
    s[n++] = '\r';
    s[n++] = '\n';
    s[n] = 0;
    return n;
}

uint8_t adsl_category_to_alptas_type(uint8_t adsl_category) {
    static const uint8_t kMap[18] = {0x0, 0x8, 0x8, 0x3, 0x1, 0xB, 0xC, 0x7, 0x4,
                                     0x8, 0x3, 0xD, 0xD, 0xD, 0x0, 0x0, 0x0, 0x0};
    return adsl_category < 18 ? kMap[adsl_category] : 0;
}

// INFO: fc 03aug26 SkyDemon draws nothing at IDType 0, so ICAO is 1 and all else 2 (README.md)
uint8_t address_mapping_table_to_idtype(uint8_t address_mapping_table) {
    return address_mapping_table == 0x05 ? 1 : 2;
}

int format_pflaa(char* out, size_t capacity, const model::Ownship& own,
                 const model::AircraftObservation& target, uint8_t alarm_level,
                 const char* callsign) {
    (void)capacity;
    int32_t north_m, east_m, up_m;
    if (!flight::relative_north_east_up(own, target, north_m, east_m, up_m)) return 0;
    int n = 0;
    n += format_string(out + n, "$PFLAA,");
    n += format_uint(out + n, alarm_level);
    out[n++] = ',';
    n += format_int(out + n, north_m, 1, 0, false);
    out[n++] = ',';
    n += format_int(out + n, east_m, 1, 0, false);
    out[n++] = ',';
    n += format_int(out + n, up_m, 1, 0, false);
    out[n++] = ',';
    n += format_uint(out + n, address_mapping_table_to_idtype(target.address_mapping_table));
    out[n++] = ',';
    n += format_hex(out + n, target.address, 6);
    if (callsign != nullptr && callsign[0] != 0) {
        out[n++] = '!';
        n += format_string(out + n, callsign);
    }
    out[n++] = ',';
    if (!flight::on_ground(target.flight_state)) {
        uint16_t deg = to_degrees(Cordic9(target.track_c9)).v;
        n += format_uint(out + n, deg);
    }
    out[n++] = ',';
    out[n++] = ',';
    if (target.speed_valid)
        n += format_uint(out + n, (static_cast<uint32_t>(target.speed_q) + 2) / 4);
    out[n++] = ',';
    if (target.climb_valid) {
        int32_t climb_dm = (static_cast<int32_t>(target.climb_e8) * 10 + 4) / 8;
        n += format_int(out + n, climb_dm, 1, 1, false);
    } else {
        out[n++] = '0';
    }
    out[n++] = ',';
    out[n++] = hex_digit(adsl_category_to_alptas_type(target.aircraft_category));
    return nmea_finish(out, n);
}

// INFO: fc 19sep26 Unknown is not a claim of being on the ground, so only Ground answers 1.
uint8_t pflau_gps(const model::Ownship& own) {
    if (!own.fix_valid) return 0;
    return flight::state_from(own.flight_state) == flight::FlightState::Ground ? 1 : 2;
}

int format_pflau(char* out, size_t capacity, const model::Ownship& own, bool transmitting,
                 int target_count, const model::AircraftObservation* threat, uint8_t alarm_level,
                 int16_t relative_bearing_deg, int32_t relative_altitude_m,
                 int32_t relative_distance_m) {
    (void)capacity;
    int n = 0;
    n += format_string(out + n, "$PFLAU,");
    n += format_uint(out + n, static_cast<uint32_t>(target_count));
    out[n++] = ',';
    out[n++] = transmitting ? '1' : '0';
    out[n++] = ',';
    out[n++] = static_cast<char>('0' + pflau_gps(own));
    out[n++] = ',';
    out[n++] = '1';
    out[n++] = ',';
    n += format_uint(out + n, alarm_level);
    out[n++] = ',';
    if (threat) {
        n += format_int(out + n, static_cast<int32_t>(relative_bearing_deg), 1, 0, false);
        out[n++] = ',';
        n += format_uint(out + n, alarm_level ? 2u : 0u);
        out[n++] = ',';
        n += format_int(out + n, relative_altitude_m, 1, 0, false);
        out[n++] = ',';
        n += format_uint(out + n,
                         static_cast<uint32_t>(relative_distance_m < 0 ? 0 : relative_distance_m));
        out[n++] = ',';
        n += format_hex(out + n, threat->address, 6);
    } else {
        out[n++] = ',';
        out[n++] = '0';
        out[n++] = ',';
        out[n++] = ',';
        out[n++] = ',';
    }
    return nmea_finish(out, n);
}

int format_pgrmz(char* out, size_t capacity, int32_t altitude_ft, bool fix_valid) {
    (void)capacity;
    int n = 0;
    n += format_string(out + n, "$PGRMZ,");
    n += format_int(out + n, altitude_ft, 1, 0, false);
    n += format_string(out + n, ",F,");
    out[n++] = fix_valid ? '3' : '1';
    return nmea_finish(out, n);
}

// Field order, from the LK8000 definition and the three senders that agree on
// it (SoftRF lyusupov src/protocol/data/NMEA.cpp:246-256, SoftRF MB
// src/driver/Baro.cpp:404-415, GXAirCom src/main.cpp:4370-4385):
//   1 raw pressure, pascals (a hundredth of a millibar)
//   2 altitude, metres on the 1013.25 datum - a consumer that got field 1
//     recomputes this for itself and ignores what is here
//   3 vertical speed, centimetres per second
//   4 temperature, degrees Celsius
//   5 battery: volts below 1000, percent plus 1000 at or above it
int format_lk8ex1(char* out, size_t capacity, const Lk8Ex1& reading) {
    (void)capacity;
    const uint32_t pressure =
        reading.pressure_valid
            ? (reading.pressure_pa > kLk8MaxPressurePa ? kLk8MaxPressurePa : reading.pressure_pa)
            : kLk8NoPressurePa;
    const int32_t altitude =
        reading.altitude_valid ? std::clamp(reading.altitude_m, kLk8MinAltitudeM, kLk8MaxAltitudeM)
                               : kLk8NoAltitudeM;
    const int32_t vario = reading.vario_valid
                              ? std::clamp(reading.vario_cm_s, kLk8MinVarioCmS, kLk8MaxVarioCmS)
                              : kLk8NoVarioCmS;
    const int32_t temperature =
        reading.temperature_valid
            ? std::clamp(reading.temperature_c, kLk8MinTemperatureC, kLk8MaxTemperatureC)
            : kLk8NoTemperatureC;
    const uint32_t percent = reading.battery_percent > 100 ? 100u : reading.battery_percent;
    const uint32_t battery =
        reading.battery_valid ? kLk8BatteryPercentBase + percent : kLk8NoBattery;

    int n = 0;
    n += format_string(out + n, "$LK8EX1,");
    n += format_uint(out + n, pressure);
    out[n++] = ',';
    n += format_int(out + n, altitude, 1, 0, false);
    out[n++] = ',';
    n += format_int(out + n, vario, 1, 0, false);
    out[n++] = ',';
    n += format_int(out + n, temperature, 1, 0, false);
    out[n++] = ',';
    n += format_uint(out + n, battery);
    return nmea_finish(out, n);
}

namespace {

// The inverse of the epoch core/gnss/nmea.cpp builds from a $GPRMC date and
// time: own.utc_s keeps no calendar fields of its own, so writing one back out
// means undoing the conversion. Howard Hinnant's civil_from_days, exact over
// the whole range a uint32_t epoch can name.
void civil_from_epoch(uint32_t utc_s, int& year, int& month, int& day, int& hh, int& mm, int& ss) {
    const uint32_t second_of_day = utc_s % 86400u;
    hh = static_cast<int>(second_of_day / 3600u);
    mm = static_cast<int>((second_of_day / 60u) % 60u);
    ss = static_cast<int>(second_of_day % 60u);
    const int64_t z = static_cast<int64_t>(utc_s / 86400u) + 719468;
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

int put_hhmmss(char* out, uint32_t utc_s) {
    const uint32_t second_of_day = utc_s % 86400u;
    int n = 0;
    n += format_uint(out + n, second_of_day / 3600u, 2);
    n += format_uint(out + n, (second_of_day / 60u) % 60u, 2);
    n += format_uint(out + n, second_of_day % 60u, 2);
    return n;
}

}  // namespace

int format_gprmc(char* out, size_t capacity, const model::Ownship& own) {
    (void)capacity;
    if (!own.fix_valid || !own.utc_valid) return 0;
    int year, month, day, hh, mm, ss;
    civil_from_epoch(own.utc_s, year, month, day, hh, mm, ss);
    int n = 0;
    n += format_string(out + n, "$GPRMC,");
    n += format_uint(out + n, static_cast<uint32_t>(hh), 2);
    n += format_uint(out + n, static_cast<uint32_t>(mm), 2);
    n += format_uint(out + n, static_cast<uint32_t>(ss), 2);
    n += format_string(out + n, ",A,");
    n += format_nmea_latitude(out + n, own.latitude_1e7);
    out[n++] = ',';
    n += format_nmea_longitude(out + n, own.longitude_1e7);
    out[n++] = ',';
    // Ground speed is knots on this sentence, kept to one decimal.
    const uint32_t knots_e1 = static_cast<uint32_t>(
        div_round<int64_t>(static_cast<int64_t>(own.speed_mm_s) * 194384, 10000000));
    n += format_uint(out + n, knots_e1, 1, 1);
    out[n++] = ',';
    n += format_uint(out + n, to_degrees(CentiDegrees(own.track_cdeg)).v, 1);
    out[n++] = ',';
    n += format_uint(out + n, static_cast<uint32_t>(day), 2);
    n += format_uint(out + n, static_cast<uint32_t>(month), 2);
    n += format_uint(out + n, static_cast<uint32_t>(year % 100), 2);
    n += format_string(out + n, ",,,A");
    return nmea_finish(out, n);
}

int format_gpgga(char* out, size_t capacity, const model::Ownship& own) {
    (void)capacity;
    if (!own.fix_valid || !own.utc_valid) return 0;
    int n = 0;
    n += format_string(out + n, "$GPGGA,");
    n += put_hhmmss(out + n, own.utc_s);
    out[n++] = ',';
    n += format_nmea_latitude(out + n, own.latitude_1e7);
    out[n++] = ',';
    n += format_nmea_longitude(out + n, own.longitude_1e7);
    n += format_string(out + n, ",1,");
    n += format_uint(out + n, own.satellites, 2);
    out[n++] = ',';
    n += format_uint(out + n, own.hdop_e2, 3, 2);
    out[n++] = ',';
    n += format_int(out + n, to_metres(Millimetres(own.altitude_msl_mm)).v, 1, 0, false);
    n += format_string(out + n, ",M,");
    n += format_int(out + n, to_metres(Millimetres(own.altitude_hae_mm - own.altitude_msl_mm)).v, 1,
                    0, false);
    n += format_string(out + n, ",M,,");
    return nmea_finish(out, n);
}

}
