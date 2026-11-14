#ifndef SKYBLIP_HARDWARE_PARTS_GNSS_TRUTH_H
#define SKYBLIP_HARDWARE_PARTS_GNSS_TRUTH_H

#include <cmath>
#include <cstdint>

#include "core/util/format.h"
#include "protocols/common/geometry.h"

namespace skyblip::models {

class GnssTruth {
   public:
    int32_t latitude_1e7{485000000};
    int32_t longitude_1e7{85000000};
    // INFO: fc 11nov26 above the ellipsoid: a GGA reports altitude_m minus geoid_separation_m
    int32_t altitude_m{1000};
    int32_t geoid_separation_m{47};
    // INFO: fc 19sep26 RMC field 7 is knots to a hundredth, so the part holds mm/s and rounds there
    int32_t speed_mm_s{23150};
    int32_t track_deg{90};
    // INFO: fc 03aug26 positive right: own-ship turns only off this track, so thermals fly here
    double turn_dps{0};
    // INFO: fc 18sep26 millimetres per second, so any rate a pilot can ask for is one the air holds
    int32_t climb_mm_s{0};
    uint32_t utc_seconds_of_day{12 * 3600 + 34 * 60 + 56};
    // INFO: fc 11nov26 ddmmyy, and "010180" is the fake date of a receiver with no almanac
    const char* date{"010125"};

    int32_t altitude_mm() const {
        return altitude_m * 1000 + static_cast<int32_t>(climbed_m_ * 1000);
    }
    // INFO: fc 18sep26 the heading flown, fraction included: NMEA's whole degree is the report
    double heading_deg() const { return track_deg + turned_deg_; }

   protected:
    // INFO: fc 18sep26 a receiver solves continuously and reports on its cadence, ADS-L 4 §C.2.5
    void fly(int32_t elapsed_ms) {
        const double speed_mps = speed_mm_s / 1000.0;
        const double elapsed_s = elapsed_ms / 1000.0;
        const double track_rad = mid_step_track_deg(elapsed_s) * 3.14159265358979 / 180.0;
        advance_latitude(speed_mps * std::cos(track_rad) * elapsed_s);
        const double latitude_cosine = std::cos(latitude_1e7 / 1e7 * 3.14159265358979 / 180.0);
        if (latitude_cosine > 0.01)
            advance_longitude(speed_mps * std::sin(track_rad) * elapsed_s, latitude_cosine);
        advance_track(elapsed_s);
        advance_altitude(elapsed_s);
        advance_clock(elapsed_ms);
    }

    // INFO: fc 19sep26 a real receiver reports hundredths of a knot and of a degree, and decimetres
    uint32_t knots_e2() const {
        return static_cast<uint32_t>(wire::divide_rounded<int64_t>(
            static_cast<int64_t>(speed_mm_s < 0 ? 0 : speed_mm_s) * 194384, 1000000));
    }

    uint32_t kmh_e2() const {
        return static_cast<uint32_t>(wire::divide_rounded<int64_t>(
            static_cast<int64_t>(speed_mm_s < 0 ? 0 : speed_mm_s) * 36, 100));
    }

    uint32_t heading_e2() const {
        const double wrapped = heading_deg() - 360.0 * static_cast<int>(heading_deg() / 360.0);
        const double positive = wrapped < 0 ? wrapped + 360.0 : wrapped;
        return static_cast<uint32_t>(std::lround(positive * 100.0)) % 36000u;
    }

    int32_t msl_dm() const {
        const int32_t msl_mm = altitude_mm() - geoid_separation_m * 1000;
        return (msl_mm >= 0 ? msl_mm + 50 : msl_mm - 50) / 100;
    }

    int put_time(char* out) const {
        int written = 0;
        written += format_unsigned(out + written, utc_seconds_of_day / 3600u, 2);
        written += format_unsigned(out + written, (utc_seconds_of_day / 60u) % 60u, 2);
        written += format_unsigned(out + written, utc_seconds_of_day % 60u, 2);
        return written;
    }

   private:
    uint32_t seconds_of_day_ms_{0};
    double climbed_m_{0};
    double turned_deg_{0};
    double latitude_fraction_1e7_{0};
    double longitude_fraction_1e7_{0};

    double mid_step_track_deg(double elapsed_s) const {
        return track_deg + turn_dps * elapsed_s * 0.5;
    }

    // INFO: fc 18sep26 a step moves less than the 1.1 cm 1e-7 deg carries, so the rest is banked
    void advance_latitude(double north_m) {
        latitude_fraction_1e7_ += north_m * 1e7 / 111320.0;
        const int32_t whole = static_cast<int32_t>(latitude_fraction_1e7_);
        latitude_fraction_1e7_ -= whole;
        latitude_1e7 += whole;
    }

    void advance_longitude(double east_m, double latitude_cosine) {
        longitude_fraction_1e7_ += east_m * 1e7 / (111320.0 * latitude_cosine);
        const int32_t whole = static_cast<int32_t>(longitude_fraction_1e7_);
        longitude_fraction_1e7_ -= whole;
        longitude_1e7 += whole;
    }

    void advance_track(double elapsed_s) {
        turned_deg_ += turn_dps * elapsed_s;
        const int32_t whole_deg = static_cast<int32_t>(turned_deg_);
        turned_deg_ -= whole_deg;
        track_deg = ((track_deg + whole_deg) % 360 + 360) % 360;
    }

    void advance_altitude(double elapsed_s) {
        climbed_m_ += climb_mm_s * elapsed_s / 1000.0;
        const int32_t whole_m = static_cast<int32_t>(climbed_m_);
        altitude_m += whole_m;
        climbed_m_ -= whole_m;
    }

    // INFO: fc 18sep26 NMEA carries whole seconds, so every solution of one second stamps the same
    void advance_clock(int32_t elapsed_ms) {
        seconds_of_day_ms_ += static_cast<uint32_t>(elapsed_ms);
        utc_seconds_of_day = (utc_seconds_of_day + seconds_of_day_ms_ / 1000u) % 86400u;
        seconds_of_day_ms_ %= 1000u;
    }
};

}  // namespace skyblip::models

#endif
