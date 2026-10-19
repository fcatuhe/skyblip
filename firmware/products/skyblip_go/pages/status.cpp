#include "products/skyblip_go/pages/status.h"

#include "core/units/units.h"
#include "core/util/format.h"
#include "core/util/intmath.h"
#include "products/skyblip_go/pages/page.h"

namespace skyblip::go {

namespace {
constexpr int kLeft = 4;
constexpr int kLineH = 16;
constexpr int kCellW = 6;  // the 5x7 font's advance at scale 1
constexpr int kColumn(int cell) { return kLeft + cell * kCellW; }

// One grid, in character cells: 4 label, 1 space, 5 value, 4 unit, 3 space,
// 5 value, 4 unit. Every unit string carries its own leading space, which is
// what keeps a five-character value off it. km/h then runs one cell past the
// field, which is what it costs to have every unit start in the same column.
constexpr int kValueX = kColumn(5);
constexpr int kAeroNumberEnd = kColumn(10);
constexpr int kAeroUnitX = kColumn(10);
constexpr int kAeroUnitEnd = kColumn(15);  // past a leading space and four characters
constexpr int kSiNumberEnd = kColumn(22);
constexpr int kSiUnitX = kColumn(22);

constexpr uint8_t kSatellitesForAltitude = 4;

const char* solution_dimensions(const StatusSnapshot& s) {
    if (s.fix_mode == gnss::kFixMode3D) return "3D";
    if (s.fix_mode == gnss::kFixMode2D) return "2D";
    return s.sats >= kSatellitesForAltitude ? "3D" : "2D";
}

int format_elapsed(char* out, uint32_t seconds) {
    constexpr uint32_t kMaxSeconds = 99 * 60 + 59;
    const uint32_t capped = seconds > kMaxSeconds ? kMaxSeconds : seconds;
    int n = format_uint(out, capped / 60, 1);
    out[n++] = ':';
    n += format_uint(out + n, capped % 60, 2);
    out[n] = 0;
    return n;
}
constexpr uint32_t kImuCountCeiling = 99;
constexpr uint8_t kImuMetaInitialised = 16;

int32_t knots(int32_t speed_mm_s) { return to_knots(MillimetresPerSecond(speed_mm_s)).v; }

int32_t centi_kilometres_per_hour(int32_t speed_mm_s) { return div_round(speed_mm_s * 36, 100); }

// Draw "LABEL  value" on one row.
void row(ui::Canvas& canvas, int y, const char* label, const char* value) {
    canvas.draw_text(kLeft, y, label, true, 1);
    canvas.draw_text(kValueX, y, value, true, 1);
}

void right_aligned(ui::Canvas& canvas, int x_end, int y, const char* text, int len) {
    canvas.draw_text(x_end - len * kCellW, y, text, true, 1);
}

int length(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

// The same two columns as the numbers, for the rows whose values are words: the
// value right-aligned on the column, its unit left-aligned after it.
void text_row(ui::Canvas& canvas, int y, const char* label, const char* value, const char* unit,
              const char* value2 = "", const char* unit2 = "") {
    canvas.draw_text(kLeft, y, label, true, 1);
    right_aligned(canvas, kAeroNumberEnd, y, value, length(value));
    canvas.draw_text(kAeroUnitX, y, unit, true, 1);
    right_aligned(canvas, kSiNumberEnd, y, value2, length(value2));
    canvas.draw_text(kSiUnitX, y, unit2, true, 1);
}

struct Quantity {
    int32_t value{0};
    uint8_t decimals{0};
    const char* unit{""};
};

// Aeronautical unit in the first column, SI in the second: the pilot reads the
// left, the engineer checks the right.
void dual_row(ui::Canvas& canvas, int y, const char* label, Quantity aero, Quantity si,
              bool no_plus) {
    char buf[16];
    canvas.draw_text(kLeft, y, label, true, 1);

    int n = format_int(buf, aero.value, 1, aero.decimals, !no_plus);
    buf[n] = 0;
    right_aligned(canvas, kAeroNumberEnd, y, buf, n);
    canvas.draw_text(kAeroUnitX, y, aero.unit, true, 1);

    n = format_int(buf, si.value, 1, si.decimals, !no_plus);
    buf[n] = 0;
    right_aligned(canvas, kSiNumberEnd, y, buf, n);
    canvas.draw_text(kSiUnitX, y, si.unit, true, 1);
}

// What the sensor reads, to the tenth of a pascal it measures in.
void pressure_row(ui::Canvas& canvas, int y, uint32_t pressure_mpa) {
    char baro[12];
    const int n = format_uint(baro, div_round<uint32_t>(pressure_mpa, 100), 1, 3);
    baro[n] = 0;

    canvas.draw_text(kLeft, y, "BARO", true, 1);
    right_aligned(canvas, kAeroUnitEnd, y, baro, n);
    canvas.draw_text(kSiUnitX, y, " hPa", true, 1);
}

uint32_t at_most(uint32_t value, uint32_t ceiling) { return value > ceiling ? ceiling : value; }

int imu_sensor_error(char* out, const StatusSnapshot& s) {
    int n = format_string(out, " SE");
    n += format_uint(out + n, s.imu_errored_sensor, 1);
    out[n++] = ':';
    n += format_hex(out + n, s.imu_sensor_error, 2);
    return n;
}

int imu_traffic(char* out, const StatusSnapshot& s) {
    if (s.imu_sensor_error != 0) return imu_sensor_error(out, s);

    const bool stuck = s.imu_unparsed != 0;
    int n = format_string(out, stuck ? " U" : " B");
    n += format_uint(out + n, at_most(stuck ? s.imu_unparsed : s.imu_fifo_bytes, kImuCountCeiling),
                     1);
    if (s.imu_error != 0) {
        n += format_string(out + n, " E");
        n += format_hex(out + n, s.imu_error, 2);
    } else if (s.imu_meta != kImuMetaInitialised) {
        n += format_string(out + n, " M");
        n += format_uint(out + n, s.imu_meta, 1);
    } else {
        n += format_string(out + n, " I");
        n += format_hex(out + n, s.imu_interrupt, 2);
    }
    return n;
}

void imu_field(ui::Canvas& canvas, int y, const StatusSnapshot& s) {
    char buf[24];
    int n = format_string(buf, "IMU ");
    n += format_string(buf + n, s.imu_stage);
    if (s.imu_fault != nullptr && s.imu_fault[0] != 0) {
        n += format_string(buf + n, " ");
        n += format_string(buf + n, s.imu_fault);
    } else if (s.slip_valid) {
        n += format_string(buf + n, " ");
        n += format_int(buf + n, s.slip_mg, 1, 0, true);
        n += format_string(buf + n, "mg");
    } else {
        n += imu_traffic(buf + n, s);
    }
    buf[n] = 0;
    right_aligned(canvas, kGlassWidth - kLeft, y, buf, n);
}

// INFO: fc 05oct26 on the cable the divider reads the USB rail, so CHG stands in for the numbers
void battery_row(ui::Canvas& canvas, int y, const StatusSnapshot& s) {
    if (!s.battery_valid && !s.charging) {
        row(canvas, y, "BAT", "no sensor");
        return;
    }

    // Centivolts, so the two decimals a cell is judged on fit the value field.
    char volts[8] = "--";
    char percent[8] = "--";
    if (s.battery_valid) {
        int n = format_uint(volts, s.battery_mv, 1, 3);
        volts[n] = 0;
        n = format_uint(percent, s.battery_percent, 1);
        percent[n] = 0;
    }

    // A cell on the cable is not low whatever it reads, so the charger wins the
    // marker. Off it, the warning is the whole reason this row is on the page.
    const char* mark = s.charge == power::ChargeCondition::TooHot    ? "% HOT"
                       : s.charge == power::ChargeCondition::TooCold ? "% CLD"
                       : s.charging                                  ? "% CHG"
                       : s.battery_low                               ? "% LOW"
                                                                     : "%";
    text_row(canvas, y, "BAT", volts, " V", percent, mark);
}
}  // namespace

void draw_status(ui::Canvas& canvas, const StatusSnapshot& s) {
    canvas.clear(true);

    char buf[32];

    // Header: device address (the ADS-L identity) + the name the pilot gave it.
    int n = format_string(buf, "ID ");
    n += format_air_address(buf + n, s.address);
    buf[n] = 0;
    canvas.draw_text(kLeft, 3, buf, true, 2);
    if (s.callsign != nullptr && s.callsign[0] != 0)
        right_aligned(canvas, kGlassWidth - kLeft, 8, s.callsign, length(s.callsign));
    canvas.hline(kLeft, 21, kGlassWidth - 2 * kLeft, true);

    // Everything that is one free-form string first, then the block where every
    // number lines up in its column.
    int y = 27;

    n = format_string(buf, " ");
    if (s.utc_valid)
        n += format_seconds_of_day(buf + n, s.utc_s);
    else
        n += format_string(buf + n, "--:--:--");
    buf[n] = 0;

    if (s.fix_valid) {
        char sats[4];
        n = format_uint(sats, s.sats, 2);
        sats[n] = 0;
        text_row(canvas, y, "GNSS", sats, " SAT", "UTC", buf);
        canvas.draw_text(kValueX, y, solution_dimensions(s), true, 1);
    } else {
        char stage[16];
        n = format_string(stage, gnss::stage_name(s.stage));
        stage[n++] = ' ';
        n += format_elapsed(stage + n, s.stage_s);
        stage[n] = 0;
        text_row(canvas, y, "GNSS", "", "", "UTC", buf);
        canvas.draw_text(kValueX, y, stage, true, 1);
    }
    y += kLineH;

    // Full 1e-7 degrees on both, which is what the fix carries. A signed
    // three-digit longitude is then 16 cells wide with its label, so the
    // longitude block is anchored to the right margin rather than to a column:
    // at seven decimals nothing narrower fits every position on earth.
    if (s.fix_valid) {
        // Ten characters of latitude do not fit a five-cell value field, so it
        // ends where the first column's unit does: level with TRUE above it.
        n = format_int(buf, s.lat_1e7, 1, 7, false);
        buf[n] = 0;
        canvas.draw_text(kLeft, y, "LAT", true, 1);
        right_aligned(canvas, kAeroUnitEnd, y, buf, n);

        n = format_string(buf, "LON ");
        n += format_int(buf + n, s.lon_1e7, 1, 7, false);
        buf[n] = 0;
        right_aligned(canvas, kGlassWidth - kLeft, y, buf, n);
    } else {
        row(canvas, y, "LAT", "no fix");
    }
    y += kLineH;

    n = format_uint(buf, static_cast<uint32_t>(s.track_cdeg), 5, 2);
    buf[n] = 0;
    text_row(canvas, y, "TRK", buf, " TRUE");
    imu_field(canvas, y, s);
    y += kLineH;

    char count[8];
    n = format_uint(count, static_cast<uint32_t>(s.n_targets), 1);
    count[n] = 0;
    text_row(canvas, y, "TFC", count, "", "TX", s.transmitting ? " ON" : " OFF");
    y += kLineH;

    // The aligned block: the pressure the altitudes depend on, the geometric
    // altitude, and pressure altitude on the 1013.25 hPa standard setting - the
    // one a flight level counts in hundreds of feet. Then the motion pair.
    if (s.baro_valid)
        pressure_row(canvas, y, s.pressure_mpa);
    else
        row(canvas, y, "BARO", "no sensor");
    y += kLineH;

    dual_row(canvas, y, "GNSS", {to_feet(Millimetres(s.alt_mm)).v, 0, " ft"},
             {div_round(s.alt_mm, 100), 1, " m"}, true);
    y += kLineH;

    if (s.baro_valid)
        dual_row(canvas, y, "STD", {to_feet(Millimetres(s.alt_std_mm)).v, 0, " ft"},
                 {div_round(s.alt_std_mm, 10), 2, " m"}, true);
    y += kLineH;

    dual_row(canvas, y, "SPD", {knots(s.speed_mm_s), 0, " kt"},
             {centi_kilometres_per_hour(s.speed_mm_s), 2, " km/h"}, true);
    y += kLineH;

    dual_row(canvas, y, "VS", {to_feet_per_minute(MillimetresPerSecond(s.climb_mm_s)).v, 0, " fpm"},
             {s.climb_mm_s, 3, " m/s"}, false);
    y += kLineH;

    battery_row(canvas, y, s);
}

}  // namespace skyblip::go
