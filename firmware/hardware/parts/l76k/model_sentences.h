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
    char s[128];
    int n = fmt_string(s, "$GPTXT,01,01,02,SW=");
    n += fmt_string(s + n, firmware_version);
    n = protocol::nmea_finish(s, n);
    pending_.append(s, static_cast<size_t>(n));
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
    const int32_t mm = alt_mm() - geoid_separation_m * 1000;
    return (mm >= 0 ? mm + 50 : mm - 50) / 100;
}

inline int L76k::put_time(char* s) const {
    int n = 0;
    n += fmt_uint(s + n, utc_sod / 3600u, 2);
    n += fmt_uint(s + n, (utc_sod / 60u) % 60u, 2);
    n += fmt_uint(s + n, utc_sod % 60u, 2);
    return n;
}

// $GPRMC,hhmmss,A,ddmm.mmmm,N,dddmm.mmmm,E,speed_kn,track,ddmmyy,,,A*cs
inline void L76k::emit_rmc() {
    char s[128];
    int n = fmt_string(s, "$GPRMC,");
    n += put_time(s + n);
    n += fmt_string(s + n, solving() ? ",A," : ",V,");
    n += fmt_nmea_lat(s + n, walked_lat_1e7());
    s[n++] = ',';
    n += fmt_nmea_lon(s + n, lon_1e7);
    s[n++] = ',';
    n += fmt_uint(s + n, knots_e2(), 1, 2);
    s[n++] = ',';
    n += fmt_uint(s + n, heading_e2(), 1, 2);
    s[n++] = ',';
    n += fmt_string(s + n, date);
    n += fmt_string(s + n, ",,,A");
    n = protocol::nmea_finish(s, n);
    pending_.append(s, static_cast<size_t>(n));
}

// INFO: fc 18sep26 one GSA per constellation, each naming its own satellites and system id
inline void L76k::emit_gsa() {
    const uint8_t budget = solving() ? sats : uint8_t{0};
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
    char s[128];
    int n = fmt_string(s, "$GNGSA,A,");
    n += fmt_uint(s + n, solving() ? 3u : 1u, 1);
    for (int slot = 0; slot < kGsaSlots; slot++) {
        s[n++] = ',';
        if (slot < used) n += fmt_uint(s + n, static_cast<uint32_t>(first_id + slot), 2);
    }
    s[n++] = ',';
    if (solving() && used > 0) {
        n += fmt_uint(s + n, pdop_e2, 3, 2);
        s[n++] = ',';
        n += fmt_uint(s + n, hdop_e2, 3, 2);
        s[n++] = ',';
        n += fmt_uint(s + n, vdop_e2, 3, 2);
    } else {
        n += fmt_string(s + n, ",,");
    }
    s[n++] = ',';
    n += fmt_uint(s + n, system_id, 1);
    n = protocol::nmea_finish(s, n);
    pending_.append(s, static_cast<size_t>(n));
}

inline void L76k::emit_gll() {
    char s[128];
    int n = fmt_string(s, "$GPGLL,");
    n += fmt_nmea_lat(s + n, walked_lat_1e7());
    s[n++] = ',';
    n += fmt_nmea_lon(s + n, lon_1e7);
    s[n++] = ',';
    n += put_time(s + n);
    n += fmt_string(s + n, solving() ? ",A,A" : ",V,N");
    n = protocol::nmea_finish(s, n);
    pending_.append(s, static_cast<size_t>(n));
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
    for (int sentence = 1; sentence <= sentences; sentence++) {
        char s[128];
        int n = fmt_string(s, "$");
        n += fmt_string(s + n, talker);
        n += fmt_string(s + n, "GSV,");
        n += fmt_uint(s + n, static_cast<uint32_t>(sentences), 1);
        s[n++] = ',';
        n += fmt_uint(s + n, static_cast<uint32_t>(sentence), 1);
        s[n++] = ',';
        n += fmt_uint(s + n, in_view, 2);
        for (int slot = 0; slot < 4 && at < in_view; slot++, at++) {
            const uint8_t id = static_cast<uint8_t>(first_id + at);
            s[n++] = ',';
            n += fmt_uint(s + n, id, 2);
            s[n++] = ',';
            n += fmt_uint(s + n, static_cast<uint32_t>(15 + (at * 7) % 70), 2);
            s[n++] = ',';
            n += fmt_uint(s + n, static_cast<uint32_t>((at * 47) % 360), 3);
            s[n++] = ',';
            if (at < tracked_of(in_view))
                n += fmt_uint(s + n, static_cast<uint32_t>(cn0_dbhz_base - at), 2);
        }
        n += fmt_string(s + n, ",0");
        n = protocol::nmea_finish(s, n);
        pending_.append(s, static_cast<size_t>(n));
    }
}

inline uint8_t L76k::tracked_of(uint8_t in_view) const {
    return solving() ? in_view : static_cast<uint8_t>(in_view / 2);
}

inline void L76k::emit_vtg() {
    char s[128];
    int n = fmt_string(s, "$GPVTG,");
    n += fmt_uint(s + n, heading_e2(), 1, 2);
    n += fmt_string(s + n, ",T,,M,");
    n += fmt_uint(s + n, knots_e2(), 1, 2);
    n += fmt_string(s + n, ",N,");
    n += fmt_uint(s + n, kmh_e2(), 1, 2);
    n += fmt_string(s + n, ",K,A");
    n = protocol::nmea_finish(s, n);
    pending_.append(s, static_cast<size_t>(n));
}

// $GPGGA,hhmmss,ddmm.mmmm,N,dddmm.mmmm,E,q,sats,hdop,msl,M,separation,M,,*cs
inline void L76k::emit_gga() {
    char s[128];
    int n = fmt_string(s, "$GPGGA,");
    n += put_time(s + n);
    s[n++] = ',';
    n += fmt_nmea_lat(s + n, walked_lat_1e7());
    s[n++] = ',';
    n += fmt_nmea_lon(s + n, lon_1e7);
    s[n++] = ',';
    n += fmt_uint(s + n, solving() ? 1u : 0u, 1);
    s[n++] = ',';
    n += fmt_uint(s + n, solving() ? sats : uint8_t{0}, 2);
    s[n++] = ',';
    n += fmt_uint(s + n, hdop_e2, 3, 2);
    s[n++] = ',';
    n += fmt_int(s + n, msl_dm(), 1, 1, true);
    n += fmt_string(s + n, ",M,");
    if (emit_geoid_separation) n += fmt_int(s + n, geoid_separation_m * 10, 1, 1, true);
    n += fmt_string(s + n, ",M,,");
    n = protocol::nmea_finish(s, n);
    pending_.append(s, static_cast<size_t>(n));
}

}  // namespace skyblip::models

#endif
