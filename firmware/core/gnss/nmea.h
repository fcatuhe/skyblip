#ifndef SKYBLIP_CORE_GNSS_NMEA_H
#define SKYBLIP_CORE_GNSS_NMEA_H

#include <cstdint>
#include <optional>

#include "core/gnss/sky.h"

namespace skyblip::gnss {

// INFO: fc 20nov26 many receivers omit GGA's geoid separation or send 0, so we assume 46 m, EGM96
// over central Europe
constexpr int32_t kDefaultGeoidSeparationMm = 46000;

// INFO: fc 20nov26 a GGA cut short still checksums, so moshe-braner's 40-byte floor refuses it
// (GNSS.cpp:2226-2236)
constexpr int kGgaLengthFloorBytes = 40;

// INFO: fc 20nov26 an MTK receiver with no almanac dates itself 1980, so OGN refuses a two-digit
// year of 70 or more (ogn.h:737-738)
constexpr int kTwoDigitYearCeiling = 70;

enum class Sentence : uint8_t { None, Rmc, Gga, Gsa, Gsv, Txt };

// INFO: fc 13sep26 GSA holds twelve satellite slots before its DOPs, so VDOP is field 17
constexpr int kGsaVdopField = 17;

// INFO: fc 18sep26 GSA field 2, the receiver's own answer: 1 no fix, 2 solved without height, 3
// with
constexpr int kGsaFixModeField = 2;
constexpr int kGsaPdopField = 15;
constexpr int kGsaFirstSatelliteField = 3;
constexpr int kGsaSatelliteSlots = 12;
constexpr int kGsaSystemField = 18;

// INFO: fc 18sep26 GSV repeats id, elevation, azimuth and C/N0 for up to four satellites a sentence
constexpr int kGsvFirstSatelliteField = 4;
constexpr int kGsvFieldsPerSatellite = 4;
constexpr uint8_t kFixMode2D = 2;
constexpr uint8_t kFixMode3D = 3;

struct GnssSolution {
    bool fix_valid{false};
    bool utc_valid{false};
    int32_t latitude_1e7{0};
    int32_t longitude_1e7{0};
    // INFO: fc 20nov26 ADS-L 4 SRD860 issue 2 G.1.7 transmits height above the WGS-84 ellipsoid,
    // and a panel or IGC wants MSL
    int32_t altitude_hae_mm{0};
    int32_t altitude_msl_mm{0};
    int32_t geoid_separation_mm{kDefaultGeoidSeparationMm};
    // INFO: fc 19sep26 what the receiver resolves, not what the wire carries: RMC fields 7 and 8
    int32_t speed_mm_s{0};
    int32_t track_cdeg{0};
    uint32_t utc_s{0};
    // INFO: fc 18nov26 the milliseconds of RMC's hhmmss.sss: at 2 Hz they name the quarter solved
    uint16_t utc_ms{0};
    uint16_t hdop_e2{0};
    // INFO: fc 13sep26 vertical DOP in hundredths, GSA field 17, zero on a 2D solution
    uint16_t vdop_e2{0};
    uint16_t pps_latency_ms{0};
    // INFO: fc 18nov26 the drain that closed it closed an older pair too: the host was late
    bool backlogged{false};
    // INFO: fc 18nov26 the part's start plus line time at the live rate, 0 when unknown
    uint16_t delivery_ms{0};
    uint8_t satellites{0};
    uint8_t fix_quality{0};
    uint8_t fix_mode{0};
    bool altitude_msl_valid{false};
    bool altitude_hae_valid{false};
    bool geoid_measured{false};
    uint32_t sentences{0};
};

class NmeaParser {
   public:
    bool feed(char character);

    bool parse_line(const char* line, int bytes);

    const GnssSolution& solution() const { return solution_; }
    void reset() { line_bytes_ = 0; }

    // INFO: fc 20nov26 meaningful only right after feed() or parse_line() returned true
    Sentence last_sentence() const { return last_sentence_; }

    const char* firmware_version() const { return firmware_version_; }
    bool identified() const { return firmware_version_[0] != 0; }

    // INFO: fc 13sep26 GLL or VTG still arriving means the sentence set was never taken
    uint32_t unrequested() const { return unrequested_; }

    const SkyView& sky() const { return sky_; }
    void forget_satellites_in_view() { sky_.forget_in_view(); }

    // INFO: fc 18nov26 the last GGA and RMC carry one UTC stamp, so height and position are one fix
    bool same_epoch() const { return gga_stamp_ms_ == rmc_stamp_ms_; }
    int32_t stamp_ms() const { return rmc_stamp_ms_; }

   private:
    static constexpr int kVersionBytes = 24;

    char line_[100];
    int line_bytes_{0};
    GnssSolution solution_;
    SkyView sky_{};
    char firmware_version_[kVersionBytes]{};
    uint32_t unrequested_{0};
    int32_t gga_stamp_ms_{-1};
    int32_t rmc_stamp_ms_{-1};
    Sentence last_sentence_{Sentence::None};
    bool solution_marks_stale_{false};

    bool apply_rmc(const char* fields[], int field_count);
    bool apply_gga(const char* fields[], int field_count, int bytes);
    bool apply_gsa(const char* fields[], int field_count);
    bool apply_gsv(const char* talker, const char* fields[], int field_count);
    bool apply_txt(const char* line, int bytes);
};

bool nmea_checksum_ok(const char* line, int bytes);
bool nmea_parse_coordinate(const char* degrees_minutes, char hemisphere, int32_t& out_1e7);

// INFO: fc 18nov26 the stated assumption: the L76K delivers a solution within a quarter
constexpr uint32_t kSolutionLatencyCeilingMs = 250;

// INFO: fc 18nov26 G.1.1's 10 ms: a stamp that close to a quarter is that quarter
constexpr int32_t kStampToleranceMs = 10;
inline std::optional<int32_t> named_quarter_ms(uint16_t utc_ms) {
    constexpr int32_t kQuarterMs = 250;
    const int32_t nearest_ms = (utc_ms + kQuarterMs / 2) / kQuarterMs * kQuarterMs;
    const int32_t off_ms = utc_ms > nearest_ms ? utc_ms - nearest_ms : nearest_ms - utc_ms;
    if (off_ms > kStampToleranceMs) return std::nullopt;
    return nearest_ms;
}

inline uint32_t solution_instant_ms(const GnssSolution& solution, uint32_t arrival_ms) {
    return arrival_ms - solution.pps_latency_ms;
}

// INFO: fc 13sep26 a locked PPS edge IS the top of the second the burst describes
// | 18nov26 and the stamp's milliseconds the phase, the latest instant with it not after arrival
inline uint32_t solution_instant_ms(const GnssSolution& solution, uint32_t arrival_ms,
                                    uint32_t pps_edge_ms, bool pps_locked) {
    if (!pps_locked) return solution_instant_ms(solution, arrival_ms);
    const uint32_t first_ms = pps_edge_ms + solution.utc_ms;
    const int32_t since_ms = static_cast<int32_t>(arrival_ms - first_ms);
    const int32_t seconds = since_ms >= 0 ? since_ms / 1000 : -((999 - since_ms) / 1000);
    return first_ms + static_cast<uint32_t>(seconds * 1000);
}

// INFO: fc 18nov26 an anchored edge names its UTC second, so a late solution keeps its own, and old
inline std::optional<uint32_t> anchored_instant_ms(const GnssSolution& solution,
                                                   uint32_t arrival_ms, uint32_t anchor_edge_ms,
                                                   uint32_t anchor_utc_s) {
    if (!solution.utc_valid || anchor_utc_s == 0) return std::nullopt;
    const int64_t offset_ms =
        (static_cast<int64_t>(solution.utc_s) - anchor_utc_s) * 1000 + solution.utc_ms;
    const int64_t late_ms =
        static_cast<int64_t>(static_cast<int32_t>(arrival_ms - anchor_edge_ms)) - offset_ms;
    if (late_ms < 0 || late_ms > INT32_MAX) return std::nullopt;
    return arrival_ms - static_cast<uint32_t>(late_ms);
}
}

#endif
