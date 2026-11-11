#ifndef SKYBLIP_HARDWARE_MODEL_AG3335_SENTENCES_H
#define SKYBLIP_HARDWARE_MODEL_AG3335_SENTENCES_H

#include "core/protocol/nmea_out.h"
#include "core/util/format.h"
#include "hardware/parts/ag3335/model.h"

namespace skyblip::models {

inline parts::PairResult Ag3335::apply(uint16_t id, const char* arguments) {
    switch (id) {
        case 21: return parts::PairResult::Success;
        case 382: sleep_locked = argument(arguments, 0) == 1; return parts::PairResult::Success;
        case 66: apply_constellations(arguments); return parts::PairResult::Success;
        case 100:
            nmea_mode = static_cast<uint8_t>(argument(arguments, 0));
            return parts::PairResult::Success;
        case 62: return apply_sentence_rate(arguments);
        case 50: {
            const uint32_t period_ms = argument(arguments, 0);
            if (period_ms < 100 || period_ms > 1000) return parts::PairResult::ParameterError;
            solution_period_ms = period_ms;
            return parts::PairResult::Success;
        }
        case 80:
            navigation_mode = static_cast<uint8_t>(argument(arguments, 0));
            return parts::PairResult::Success;
        case 400:
            dgps_mode = static_cast<uint8_t>(argument(arguments, 0));
            return parts::PairResult::Success;
        case 410: sbas_enabled = argument(arguments, 0) == 1; return parts::PairResult::Success;
        case 752: {
            pps_type = static_cast<int>(argument(arguments, 0));
            const int width_at = next_field(arguments, 0);
            pps_width_ms = width_at < 0 ? 0 : static_cast<uint16_t>(argument(arguments, width_at));
            return parts::PairResult::Success;
        }
        case 4:
        case 5:
        case 6:
        case 7: restart(id - 4); return parts::PairResult::Success;
        default: return parts::PairResult::Unsupported;
    }
}

// INFO: fc 11nov26 GPS, GLONASS, Galileo, BeiDou, QZSS and NavIC, SIM65M §2.3.36
inline void Ag3335::apply_constellations(const char* arguments) {
    uint8_t enabled = 0;
    int at = 0;
    for (int field = 0; field < 6 && at >= 0; field++) {
        if (argument(arguments, at) == 1) enabled = static_cast<uint8_t>(enabled | (1u << field));
        at = next_field(arguments, at);
    }
    constellations = enabled;
    restart_since_ms_ = last_tick_ms_;
    restarting_ = true;
}

inline parts::PairResult Ag3335::apply_sentence_rate(const char* arguments) {
    const int rate_at = next_field(arguments, 0);
    if (arguments[0] < '0' || arguments[0] > '9' || rate_at < 0)
        return parts::PairResult::ParameterError;
    const uint32_t type = argument(arguments, 0);
    const uint32_t every = argument(arguments, rate_at);
    if (type >= kSentenceTypes || every > 5) return parts::PairResult::ParameterError;
    sentence_every[type] = static_cast<uint8_t>(every);
    return parts::PairResult::Success;
}

// INFO: fc 11nov26 $PAIR007 also restores the factory settings, SIM65M §2.3.7
inline void Ag3335::restart(int kind) {
    restarts++;
    last_restart_kind = kind;
    restart_since_ms_ = last_tick_ms_;
    restarting_ = true;
    if (kind < 3) return;
    factory_resets++;
    for (uint8_t& every : sentence_every) every = 1;
    solution_period_ms = kFactoryPeriodMs;
    constellations = 0;
    navigation_mode = 1;
    nmea_mode = 1;
    dgps_mode = 2;
    sbas_enabled = true;
    sleep_locked = false;
    pps_type = kPpsUnstated;
    pps_width_ms = 0;
}

inline void Ag3335::acknowledge(uint16_t id, parts::PairResult result) {
    if (acks_to_drop > 0) {
        acks_to_drop--;
        return;
    }
    char line[32];
    int written = format_string(line, "$PAIR001,");
    written += format_unsigned(line + written, id, 3);
    line[written++] = ',';
    written += format_unsigned(line + written, static_cast<uint32_t>(result), 1);
    finish(line, written);
    if (id == 21 && result == parts::PairResult::Success) emit_setting();
}

// INFO: fc 11nov26 the fields after the frequency are not modelled, since nothing reads them
inline void Ag3335::emit_setting() {
    char line[96];
    int written = format_string(line, "$PAIR021,");
    written += format_string(line + written, project_version);
    line[written++] = ',';
    line[written++] = frequency;
    written += format_string(line + written, ",N");
    finish(line, written);
}

// INFO: fc 11nov26 the order Meshtastic issue #4626 logged, with RMC after GSA and GSV
inline void Ag3335::emit_burst() {
    const uint32_t solution = solutions_++;
    const auto due = [&](int type) {
        return sentence_every[type] != 0 && solution % sentence_every[type] == 0;
    };
    if (due(kGga)) emit_gga();
    if (due(kGll)) emit_gll();
    if (due(kGsa)) emit_gsa();
    if (due(kRmc)) emit_rmc();
    if (due(kVtg)) emit_vtg();
    if (due(kZda)) emit_zda();
}

inline int Ag3335::put_stamp(char* out) const {
    int written = put_time(out);
    written += format_string(out + written, ".000");
    return written;
}

inline void Ag3335::finish(char* line, int written) {
    written = protocol::nmea_finish(line, written);
    pending_.append(line, static_cast<size_t>(written));
}

// INFO: fc 11nov26 the no-fix form is Meshtastic issue #4626's "$GNGGA,000008.012,,,,,0,0,,,M,,M,,"
inline void Ag3335::emit_gga() {
    char line[128];
    int written = format_string(line, "$GNGGA,");
    written += put_stamp(line + written);
    if (!fix_valid) {
        written += format_string(line + written, ",,,,,0,0,,,M,,M,,");
        finish(line, written);
        return;
    }
    line[written++] = ',';
    written += format_nmea_latitude(line + written, latitude_1e7);
    line[written++] = ',';
    written += format_nmea_longitude(line + written, longitude_1e7);
    written += format_string(line + written, ",1,");
    written += format_unsigned(line + written, satellites, 2);
    line[written++] = ',';
    written += format_unsigned(line + written, hdop_e2, 1, 2);
    line[written++] = ',';
    written += format_signed(line + written, msl_dm(), 1, 1, false);
    written += format_string(line + written, ",M,");
    if (emit_geoid_separation)
        written += format_signed(line + written, geoid_separation_m * 10, 1, 1, false);
    written += format_string(line + written, ",M,,");
    finish(line, written);
}

inline void Ag3335::emit_gll() {
    char line[96];
    int written = format_string(line, "$GNGLL,");
    if (fix_valid) {
        written += format_nmea_latitude(line + written, latitude_1e7);
        line[written++] = ',';
        written += format_nmea_longitude(line + written, longitude_1e7);
        line[written++] = ',';
    } else {
        written += format_string(line + written, ",,,,");
    }
    written += put_stamp(line + written);
    written += format_string(line + written, fix_valid ? ",A,A" : ",V,N");
    finish(line, written);
}

// INFO: fc 11nov26 one GSA a system, ids 1 GPS, 2 GLONASS, 3 Galileo, 4 BeiDou, 6 NavIC
inline void Ag3335::emit_gsa() {
    struct System {
        uint8_t bit;
        uint8_t id;
        uint8_t first_satellite;
    };
    static constexpr System kSystems[] = {
        {kGps, 1, 1}, {kGlonass, 2, 65}, {kGalileo, 3, 1}, {kBeidou, 4, 1}, {kNavic, 6, 1},
    };
    int emitted = 0;
    for (const System& system : kSystems)
        if (constellations == 0 || (constellations & system.bit) != 0) emitted++;
    const uint8_t budget = fix_valid ? satellites : uint8_t{0};
    int remainder = emitted > 0 ? budget % emitted : 0;
    for (const System& system : kSystems) {
        if (constellations != 0 && (constellations & system.bit) == 0) continue;
        int used = budget / emitted + (remainder > 0 ? 1 : 0);
        if (remainder > 0) remainder--;
        if (used > kGsaSlots) used = kGsaSlots;
        emit_gsa_for(system.id, system.first_satellite, static_cast<uint8_t>(used));
    }
}

inline void Ag3335::emit_gsa_for(uint8_t system_id, uint8_t first_id, uint8_t used) {
    char line[128];
    int written = format_string(line, "$GNGSA,A,");
    written += format_unsigned(line + written, fix_valid ? 3u : 1u, 1);
    for (int slot = 0; slot < kGsaSlots; slot++) {
        line[written++] = ',';
        if (slot < used)
            written += format_unsigned(line + written, static_cast<uint32_t>(first_id + slot), 2);
    }
    line[written++] = ',';
    if (fix_valid && used > 0) {
        written += format_unsigned(line + written, pdop_e2, 1, 2);
        line[written++] = ',';
        written += format_unsigned(line + written, hdop_e2, 1, 2);
        line[written++] = ',';
        written += format_unsigned(line + written, vdop_e2, 1, 2);
    } else {
        written += format_string(line + written, ",,");
    }
    line[written++] = ',';
    written += format_unsigned(line + written, system_id, 1);
    finish(line, written);
}

// INFO: fc 11nov26 NMEA 4.10 RMC ends in mode and navigational status, SIM65M §2.2.5
inline void Ag3335::emit_rmc() {
    char line[128];
    int written = format_string(line, "$GNRMC,");
    written += put_stamp(line + written);
    if (fix_valid) {
        written += format_string(line + written, ",A,");
        written += format_nmea_latitude(line + written, latitude_1e7);
        line[written++] = ',';
        written += format_nmea_longitude(line + written, longitude_1e7);
        line[written++] = ',';
        written += format_unsigned(line + written, knots_e2(), 1, 2);
        line[written++] = ',';
        written += format_unsigned(line + written, heading_e2(), 1, 2);
    } else {
        written += format_string(line + written, ",V,,,,,,");
    }
    line[written++] = ',';
    written += format_string(line + written, date);
    written += format_string(line + written, fix_valid ? ",,,A,V" : ",,,N,V");
    finish(line, written);
}

inline void Ag3335::emit_vtg() {
    char line[96];
    int written = format_string(line, "$GNVTG,");
    if (!fix_valid) {
        written += format_string(line + written, ",T,,M,,N,,K,N");
        finish(line, written);
        return;
    }
    written += format_unsigned(line + written, heading_e2(), 1, 2);
    written += format_string(line + written, ",T,,M,");
    written += format_unsigned(line + written, knots_e2(), 1, 2);
    written += format_string(line + written, ",N,");
    written += format_unsigned(line + written, kmh_e2(), 1, 2);
    written += format_string(line + written, ",K,A");
    finish(line, written);
}

inline void Ag3335::emit_zda() {
    char line[64];
    int written = format_string(line, "$GNZDA,");
    written += put_stamp(line + written);
    line[written++] = ',';
    line[written++] = date[0];
    line[written++] = date[1];
    line[written++] = ',';
    line[written++] = date[2];
    line[written++] = date[3];
    line[written++] = ',';
    written += format_string(line + written, date[4] >= '7' ? "19" : "20");
    line[written++] = date[4];
    line[written++] = date[5];
    written += format_string(line + written, ",,");
    finish(line, written);
}

}  // namespace skyblip::models

#endif
