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

int nmea_finish(char* sentence, int body_bytes) {
    uint8_t checksum = 0;
    for (int i = 1; i < body_bytes; i++) checksum ^= static_cast<uint8_t>(sentence[i]);
    int written = body_bytes;
    sentence[written++] = '*';
    sentence[written++] = hex_digit(static_cast<uint8_t>(checksum >> 4));
    sentence[written++] = hex_digit(static_cast<uint8_t>(checksum & 0x0F));
    sentence[written++] = '\r';
    sentence[written++] = '\n';
    sentence[written] = 0;
    return written;
}

uint8_t adsl_category_to_alptas_type(uint8_t adsl_category) {
    static const uint8_t kMap[18] = {0x0, 0x8, 0x8, 0x3, 0x1, 0xB, 0xC, 0x7, 0x4,
                                     0x8, 0x3, 0xD, 0xD, 0xD, 0x0, 0x0, 0x0, 0x0};
    return adsl_category < 18 ? kMap[adsl_category] : 0;
}

// INFO: fc 03aug26 SkyDemon draws nothing at IDType 0, so ICAO is 1 and all else 2 (README.md)
uint8_t address_mapping_table_to_id_type(uint8_t address_mapping_table) {
    return address_mapping_table == 0x05 ? 1 : 2;
}

int format_pflaa(char* out, size_t capacity, const model::Ownship& own,
                 const model::AircraftObservation& target, uint8_t alarm_level,
                 const char* callsign) {
    (void)capacity;
    int32_t north_m, east_m, up_m;
    if (!flight::relative_north_east_up(own, target, north_m, east_m, up_m)) return 0;
    int written = 0;
    written += format_string(out + written, "$PFLAA,");
    written += format_unsigned(out + written, alarm_level);
    out[written++] = ',';
    written += format_signed(out + written, north_m, 1, 0, false);
    out[written++] = ',';
    written += format_signed(out + written, east_m, 1, 0, false);
    out[written++] = ',';
    written += format_signed(out + written, up_m, 1, 0, false);
    out[written++] = ',';
    written += format_unsigned(out + written,
                               address_mapping_table_to_id_type(target.address_mapping_table));
    out[written++] = ',';
    written += format_hex(out + written, target.address, 6);
    if (callsign != nullptr && callsign[0] != 0) {
        out[written++] = '!';
        written += format_string(out + written, callsign);
    }
    out[written++] = ',';
    if (!flight::on_ground(target.flight_state)) {
        uint16_t track_deg = to_degrees(Cordic9(target.track_c9)).value;
        written += format_unsigned(out + written, track_deg);
    }
    out[written++] = ',';
    out[written++] = ',';
    if (target.speed_valid)
        written += format_unsigned(out + written, (static_cast<uint32_t>(target.speed_q) + 2) / 4);
    out[written++] = ',';
    if (target.climb_valid) {
        int32_t climb_dm = (static_cast<int32_t>(target.climb_e8) * 10 + 4) / 8;
        written += format_signed(out + written, climb_dm, 1, 1, false);
    } else {
        out[written++] = '0';
    }
    out[written++] = ',';
    out[written++] = hex_digit(adsl_category_to_alptas_type(target.aircraft_category));
    return nmea_finish(out, written);
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
    int written = 0;
    written += format_string(out + written, "$PFLAU,");
    written += format_unsigned(out + written, static_cast<uint32_t>(target_count));
    out[written++] = ',';
    out[written++] = transmitting ? '1' : '0';
    out[written++] = ',';
    out[written++] = static_cast<char>('0' + pflau_gps(own));
    out[written++] = ',';
    out[written++] = '1';
    out[written++] = ',';
    written += format_unsigned(out + written, alarm_level);
    out[written++] = ',';
    if (threat) {
        written +=
            format_signed(out + written, static_cast<int32_t>(relative_bearing_deg), 1, 0, false);
        out[written++] = ',';
        written += format_unsigned(out + written, alarm_level ? 2u : 0u);
        out[written++] = ',';
        written += format_signed(out + written, relative_altitude_m, 1, 0, false);
        out[written++] = ',';
        written += format_unsigned(
            out + written,
            static_cast<uint32_t>(relative_distance_m < 0 ? 0 : relative_distance_m));
        out[written++] = ',';
        written += format_hex(out + written, threat->address, 6);
    } else {
        out[written++] = ',';
        out[written++] = '0';
        out[written++] = ',';
        out[written++] = ',';
        out[written++] = ',';
    }
    return nmea_finish(out, written);
}

int format_pgrmz(char* out, size_t capacity, int32_t altitude_ft, bool fix_valid) {
    (void)capacity;
    int written = 0;
    written += format_string(out + written, "$PGRMZ,");
    written += format_signed(out + written, altitude_ft, 1, 0, false);
    written += format_string(out + written, ",F,");
    out[written++] = fix_valid ? '3' : '1';
    return nmea_finish(out, written);
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
    const uint32_t pressure = reading.pressure_valid ? (reading.pressure_pa > kLk8PressureCeilingPa
                                                            ? kLk8PressureCeilingPa
                                                            : reading.pressure_pa)
                                                     : kLk8NoPressurePa;
    const int32_t altitude =
        reading.altitude_valid
            ? std::clamp(reading.altitude_m, kLk8AltitudeFloorM, kLk8AltitudeCeilingM)
            : kLk8NoAltitudeM;
    const int32_t vario =
        reading.vario_valid ? std::clamp(reading.vario_cm_s, kLk8VarioFloorCmS, kLk8VarioCeilingCmS)
                            : kLk8NoVarioCmS;
    const int32_t temperature =
        reading.temperature_valid
            ? std::clamp(reading.temperature_c, kLk8TemperatureFloorC, kLk8TemperatureCeilingC)
            : kLk8NoTemperatureC;
    const uint32_t percent = reading.battery_percent > 100 ? 100u : reading.battery_percent;
    const uint32_t battery =
        reading.battery_valid ? kLk8BatteryPercentBase + percent : kLk8NoBattery;

    int written = 0;
    written += format_string(out + written, "$LK8EX1,");
    written += format_unsigned(out + written, pressure);
    out[written++] = ',';
    written += format_signed(out + written, altitude, 1, 0, false);
    out[written++] = ',';
    written += format_signed(out + written, vario, 1, 0, false);
    out[written++] = ',';
    written += format_signed(out + written, temperature, 1, 0, false);
    out[written++] = ',';
    written += format_unsigned(out + written, battery);
    return nmea_finish(out, written);
}

namespace {

// The inverse of the epoch core/gnss/nmea.cpp builds from a $GPRMC date and
// time: own.utc_s keeps no calendar fields of its own, so writing one back out
// means undoing the conversion. Howard Hinnant's civil_from_days, exact over
// the whole range a uint32_t epoch can name.
void civil_from_epoch(uint32_t utc_s, int& year, int& month, int& day) {
    const int64_t days_since_march_epoch = static_cast<int64_t>(utc_s / 86400u) + 719468;
    const int64_t era =
        (days_since_march_epoch >= 0 ? days_since_march_epoch : days_since_march_epoch - 146096) /
        146097;
    const uint32_t day_of_era = static_cast<uint32_t>(days_since_march_epoch - era * 146097);
    const uint32_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const int64_t march_year = static_cast<int64_t>(year_of_era) + era * 400;
    const uint32_t day_of_year =
        day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const uint32_t march_month = (5 * day_of_year + 2) / 153;
    day = static_cast<int>(day_of_year - (153 * march_month + 2) / 5 + 1);
    month = static_cast<int>(march_month) + (march_month < 10 ? 3 : -9);
    year = static_cast<int>(march_year + (month <= 2 ? 1 : 0));
}

int format_nmea_time(char* out, uint32_t utc_s) {
    const uint32_t second_of_day = utc_s % 86400u;
    int written = 0;
    written += format_unsigned(out + written, second_of_day / 3600u, 2);
    written += format_unsigned(out + written, (second_of_day / 60u) % 60u, 2);
    written += format_unsigned(out + written, second_of_day % 60u, 2);
    return written;
}

}  // namespace

int format_gprmc(char* out, size_t capacity, const model::Ownship& own) {
    (void)capacity;
    if (!own.fix_valid || !own.utc_valid) return 0;
    int year, month, day;
    civil_from_epoch(own.utc_s, year, month, day);
    int written = 0;
    written += format_string(out + written, "$GPRMC,");
    written += format_nmea_time(out + written, own.utc_s);
    written += format_string(out + written, ",A,");
    written += format_nmea_latitude(out + written, own.latitude_1e7);
    out[written++] = ',';
    written += format_nmea_longitude(out + written, own.longitude_1e7);
    out[written++] = ',';
    // Ground speed is knots on this sentence, kept to one decimal.
    const uint32_t knots_e1 = static_cast<uint32_t>(
        div_round<int64_t>(static_cast<int64_t>(own.speed_mm_s) * 194384, 10000000));
    written += format_unsigned(out + written, knots_e1, 1, 1);
    out[written++] = ',';
    written += format_unsigned(out + written, to_degrees(CentiDegrees(own.track_cdeg)).value, 1);
    out[written++] = ',';
    written += format_unsigned(out + written, static_cast<uint32_t>(day), 2);
    written += format_unsigned(out + written, static_cast<uint32_t>(month), 2);
    written += format_unsigned(out + written, static_cast<uint32_t>(year % 100), 2);
    written += format_string(out + written, ",,,A");
    return nmea_finish(out, written);
}

int format_gpgga(char* out, size_t capacity, const model::Ownship& own) {
    (void)capacity;
    if (!own.fix_valid || !own.utc_valid) return 0;
    int written = 0;
    written += format_string(out + written, "$GPGGA,");
    written += format_nmea_time(out + written, own.utc_s);
    out[written++] = ',';
    written += format_nmea_latitude(out + written, own.latitude_1e7);
    out[written++] = ',';
    written += format_nmea_longitude(out + written, own.longitude_1e7);
    written += format_string(out + written, ",1,");
    written += format_unsigned(out + written, own.satellites, 2);
    out[written++] = ',';
    written += format_unsigned(out + written, own.hdop_e2, 3, 2);
    out[written++] = ',';
    written += format_signed(out + written, to_metres(Millimetres(own.altitude_msl_mm)).value, 1, 0,
                             false);
    written += format_string(out + written, ",M,");
    written += format_signed(
        out + written, to_metres(Millimetres(own.altitude_hae_mm - own.altitude_msl_mm)).value, 1,
        0, false);
    written += format_string(out + written, ",M,,");
    return nmea_finish(out, written);
}

}
