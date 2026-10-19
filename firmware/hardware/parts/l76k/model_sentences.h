// hardware/parts/l76k/model_sentences.h: what the L76K model says, one NMEA sentence at a time.
#ifndef SKYBLIP_HARDWARE_MODEL_L76K_SENTENCES_H
#define SKYBLIP_HARDWARE_MODEL_L76K_SENTENCES_H

#include <cmath>

#include "core/protocol/nmea_out.h"
#include "core/util/format.h"
#include "core/util/intmath.h"
#include "hardware/parts/l76k/model.h"

namespace skyblip::models {

// INFO: fc 13sep26 the part emits the cycle in $PCAS03's own field order, so RMC closes a burst
inline void L76k::emit_burst() {
    step_walk();
    const bool gsv_due = gsv_enabled() && solutions_ % gsv_every == 0;
    solutions_++;
    if (gga_enabled) emit_gga();
    if (gll_enabled) emit_gll();
    if (gsa_enabled) emit_gsa();
    if (gsv_due) emit_gsv();
    if (rmc_enabled) emit_rmc();
    if (vtg_enabled) emit_vtg();
}

// $GPTXT,01,01,02,SW=<version>: the reply SoftRF matches on
// (oss/SoftRF-lyusupov .../src/driver/GNSS.cpp:981-1010, 1015-1025).
inline void L76k::emit_version() {
    char line[128];
    int written = format_string(line, "$GPTXT,01,01,02,SW=");
    written += format_string(line + written, firmware_version);
    written = protocol::nmea_finish(line, written);
    pending_.append(line, static_cast<size_t>(written));
}

// INFO: fc 19sep26 a real receiver reports hundredths of a knot and of a degree, and decimetres
inline uint32_t L76k::knots_e2() const {
    return static_cast<uint32_t>(div_round<int64_t>(
        static_cast<int64_t>(speed_mm_s < 0 ? 0 : speed_mm_s) * 194384, 1000000));
}

inline uint32_t L76k::kmh_e2() const {
    return static_cast<uint32_t>(
        div_round<int64_t>(static_cast<int64_t>(speed_mm_s < 0 ? 0 : speed_mm_s) * 36, 100));
}

inline uint32_t L76k::heading_e2() const {
    const double wrapped = heading_deg() - 360.0 * static_cast<int>(heading_deg() / 360.0);
    const double positive = wrapped < 0 ? wrapped + 360.0 : wrapped;
    return static_cast<uint32_t>(std::lround(positive * 100.0)) % 36000u;
}

inline int32_t L76k::msl_dm() const {
    const int32_t msl_mm = altitude_mm() - geoid_separation_m * 1000;
    return (msl_mm >= 0 ? msl_mm + 50 : msl_mm - 50) / 100;
}

inline int L76k::put_time(char* out) const {
    int written = 0;
    written += format_unsigned(out + written, utc_seconds_of_day / 3600u, 2);
    written += format_unsigned(out + written, (utc_seconds_of_day / 60u) % 60u, 2);
    written += format_unsigned(out + written, utc_seconds_of_day % 60u, 2);
    return written;
}

// $GPRMC,hhmmss,A,ddmm.mmmm,N,dddmm.mmmm,E,speed_kn,track,ddmmyy,,,A*cs
inline void L76k::emit_rmc() {
    char line[128];
    int written = format_string(line, "$GPRMC,");
    written += put_time(line + written);
    written += format_string(line + written, solving() ? ",A," : ",V,");
    written += format_nmea_latitude(line + written, walked_latitude_1e7());
    line[written++] = ',';
    written += format_nmea_longitude(line + written, longitude_1e7);
    line[written++] = ',';
    written += format_unsigned(line + written, knots_e2(), 1, 2);
    line[written++] = ',';
    written += format_unsigned(line + written, heading_e2(), 1, 2);
    line[written++] = ',';
    written += format_string(line + written, date);
    written += format_string(line + written, ",,,A");
    written = protocol::nmea_finish(line, written);
    pending_.append(line, static_cast<size_t>(written));
}

// INFO: fc 18sep26 one GSA per constellation, each naming its own satellites and system id
inline void L76k::emit_gsa() {
    const uint8_t budget = solving() ? satellites : uint8_t{0};
    const uint8_t in_view = static_cast<uint8_t>(gps_in_view + beidou_in_view + glonass_in_view);
    const uint8_t gps = share_of_solution(budget, gps_in_view, in_view);
    const uint8_t beidou = share_of_solution(budget, beidou_in_view, in_view);
    emit_gsa_for(1, 1, gps_in_view, gps);
    emit_gsa_for(4, 7, beidou_in_view, beidou);
    emit_gsa_for(2, 65, glonass_in_view, static_cast<uint8_t>(budget - gps - beidou));
}

inline uint8_t L76k::share_of_solution(uint8_t budget, uint8_t in_view, uint8_t total_in_view) {
    if (total_in_view == 0) return 0;
    return static_cast<uint8_t>(budget * in_view / total_in_view);
}

inline void L76k::emit_gsa_for(uint8_t system_id, uint8_t first_id, uint8_t in_view,
                               uint8_t budget) {
    const uint8_t used = budget < in_view ? budget : in_view;
    char line[128];
    int written = format_string(line, "$GNGSA,A,");
    written += format_unsigned(line + written, solving() ? 3u : 1u, 1);
    for (int slot = 0; slot < kGsaSlots; slot++) {
        line[written++] = ',';
        if (slot < used)
            written += format_unsigned(line + written, static_cast<uint32_t>(first_id + slot), 2);
    }
    line[written++] = ',';
    if (solving() && used > 0) {
        written += format_unsigned(line + written, pdop_e2, 3, 2);
        line[written++] = ',';
        written += format_unsigned(line + written, hdop_e2, 3, 2);
        line[written++] = ',';
        written += format_unsigned(line + written, vdop_e2, 3, 2);
    } else {
        written += format_string(line + written, ",,");
    }
    line[written++] = ',';
    written += format_unsigned(line + written, system_id, 1);
    written = protocol::nmea_finish(line, written);
    pending_.append(line, static_cast<size_t>(written));
}

inline void L76k::emit_gll() {
    char line[128];
    int written = format_string(line, "$GPGLL,");
    written += format_nmea_latitude(line + written, walked_latitude_1e7());
    line[written++] = ',';
    written += format_nmea_longitude(line + written, longitude_1e7);
    line[written++] = ',';
    written += put_time(line + written);
    written += format_string(line + written, solving() ? ",A,A" : ",V,N");
    written = protocol::nmea_finish(line, written);
    pending_.append(line, static_cast<size_t>(written));
}

// INFO: fc 18sep26 one set per talker, four satellites a sentence, and no C/N0 on one not
// tracked
inline void L76k::emit_gsv() {
    emit_gsv_set("GP", gps_in_view, 1);
    emit_gsv_set("BD", beidou_in_view, 7);
    emit_gsv_set("GL", glonass_in_view, 65);
}

inline void L76k::emit_gsv_set(const char* talker, uint8_t in_view, uint8_t first_id) {
    if (in_view == 0) return;
    const int sentences = (in_view + 3) / 4;
    uint8_t at = 0;
    for (int number = 1; number <= sentences; number++) {
        char line[128];
        int written = format_string(line, "$");
        written += format_string(line + written, talker);
        written += format_string(line + written, "GSV,");
        written += format_unsigned(line + written, static_cast<uint32_t>(sentences), 1);
        line[written++] = ',';
        written += format_unsigned(line + written, static_cast<uint32_t>(number), 1);
        line[written++] = ',';
        written += format_unsigned(line + written, in_view, 2);
        for (int slot = 0; slot < 4 && at < in_view; slot++, at++) {
            const uint8_t satellite_id = static_cast<uint8_t>(first_id + at);
            line[written++] = ',';
            written += format_unsigned(line + written, satellite_id, 2);
            line[written++] = ',';
            written +=
                format_unsigned(line + written, static_cast<uint32_t>(15 + (at * 7) % 70), 2);
            line[written++] = ',';
            written += format_unsigned(line + written, static_cast<uint32_t>((at * 47) % 360), 3);
            line[written++] = ',';
            if (at < tracked_of(in_view))
                written +=
                    format_unsigned(line + written, static_cast<uint32_t>(cn0_dbhz_base - at), 2);
        }
        written += format_string(line + written, ",0");
        written = protocol::nmea_finish(line, written);
        pending_.append(line, static_cast<size_t>(written));
    }
}

inline uint8_t L76k::tracked_of(uint8_t in_view) const {
    return solving() ? in_view : static_cast<uint8_t>(in_view / 2);
}

inline void L76k::emit_vtg() {
    char line[128];
    int written = format_string(line, "$GPVTG,");
    written += format_unsigned(line + written, heading_e2(), 1, 2);
    written += format_string(line + written, ",T,,M,");
    written += format_unsigned(line + written, knots_e2(), 1, 2);
    written += format_string(line + written, ",N,");
    written += format_unsigned(line + written, kmh_e2(), 1, 2);
    written += format_string(line + written, ",K,A");
    written = protocol::nmea_finish(line, written);
    pending_.append(line, static_cast<size_t>(written));
}

// $GPGGA,hhmmss,ddmm.mmmm,N,dddmm.mmmm,E,q,sats,hdop,msl,M,separation,M,,*cs
inline void L76k::emit_gga() {
    char line[128];
    int written = format_string(line, "$GPGGA,");
    written += put_time(line + written);
    line[written++] = ',';
    written += format_nmea_latitude(line + written, walked_latitude_1e7());
    line[written++] = ',';
    written += format_nmea_longitude(line + written, longitude_1e7);
    line[written++] = ',';
    written += format_unsigned(line + written, solving() ? 1u : 0u, 1);
    line[written++] = ',';
    written += format_unsigned(line + written, solving() ? satellites : uint8_t{0}, 2);
    line[written++] = ',';
    written += format_unsigned(line + written, hdop_e2, 3, 2);
    line[written++] = ',';
    written += format_signed(line + written, msl_dm(), 1, 1, false);
    written += format_string(line + written, ",M,");
    if (emit_geoid_separation)
        written += format_signed(line + written, geoid_separation_m * 10, 1, 1, false);
    written += format_string(line + written, ",M,,");
    written = protocol::nmea_finish(line, written);
    pending_.append(line, static_cast<size_t>(written));
}

}  // namespace skyblip::models

#endif
