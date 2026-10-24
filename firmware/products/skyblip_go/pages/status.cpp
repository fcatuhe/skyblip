#include "products/skyblip_go/pages/status.h"

#include "core/units/units.h"
#include "core/util/format.h"
#include "core/util/intmath.h"
#include "products/skyblip_go/pages/page.h"

namespace skyblip::go {

namespace {
constexpr int kLeft = 4;
constexpr int kLineHeight = 16;
using ui::kCellWidth;
constexpr int column_x(int cell) { return kLeft + cell * kCellWidth; }

// One grid, in character cells: 4 label, 1 space, 5 value, 4 unit, 3 space,
// 5 value, 4 unit. Every unit string carries its own leading space, which is
// what keeps a five-character value off it. km/h then runs one cell past the
// field, which is what it costs to have every unit start in the same column.
constexpr int kValueX = column_x(5);
constexpr int kAeronauticalNumberEnd = column_x(10);
constexpr int kAeronauticalUnitX = column_x(10);
constexpr int kAeronauticalUnitEnd = column_x(15);  // past a leading space and four characters
constexpr int kMetricNumberEnd = column_x(22);
constexpr int kMetricUnitX = column_x(22);

const char* solution_dimensions(const StatusSnapshot& snapshot) {
    if (snapshot.fix_mode == gnss::kFixMode3D) return "3D";
    if (snapshot.fix_mode == gnss::kFixMode2D) return "2D";
    return snapshot.satellites >= kFewestSatellitesForAltitude ? "3D" : "2D";
}

int format_elapsed(char* out, uint32_t seconds) {
    constexpr uint32_t kElapsedCeilingS = 99 * 60 + 59;
    const uint32_t capped = seconds > kElapsedCeilingS ? kElapsedCeilingS : seconds;
    int written = format_unsigned(out, capped / 60, 1);
    out[written++] = ':';
    written += format_unsigned(out + written, capped % 60, 2);
    out[written] = 0;
    return written;
}
constexpr uint32_t kImuCountCeiling = 99;
constexpr uint8_t kImuMetaEventInitialised = 16;

int32_t knots(int32_t speed_mm_s) { return to_knots(MillimetresPerSecond(speed_mm_s)).value; }

int32_t centi_kilometres_per_hour(int32_t speed_mm_s) {
    return divide_rounded(speed_mm_s * 36, 100);
}

// Draw "LABEL  value" on one row.
void row(ui::Canvas& canvas, int y, const char* label, const char* value) {
    canvas.draw_text(kLeft, y, label, true, 1);
    canvas.draw_text(kValueX, y, value, true, 1);
}

void right_aligned(ui::Canvas& canvas, int x_end, int y, const char* text, int length) {
    canvas.draw_text(x_end - length * kCellWidth, y, text, true, 1);
}

int length(const char* text) {
    int count = 0;
    while (text[count]) count++;
    return count;
}

// The same two columns as the numbers, for the rows whose values are words: the
// value right-aligned on the column, its unit left-aligned after it.
void text_row(ui::Canvas& canvas, int y, const char* label, const char* value, const char* unit,
              const char* second_value = "", const char* second_unit = "") {
    canvas.draw_text(kLeft, y, label, true, 1);
    right_aligned(canvas, kAeronauticalNumberEnd, y, value, length(value));
    canvas.draw_text(kAeronauticalUnitX, y, unit, true, 1);
    right_aligned(canvas, kMetricNumberEnd, y, second_value, length(second_value));
    canvas.draw_text(kMetricUnitX, y, second_unit, true, 1);
}

struct Quantity {
    int32_t value{0};
    uint8_t decimals{0};
    const char* unit{""};
};

// Aeronautical unit in the first column, SI in the second: the pilot reads the
// left, the engineer checks the right.
void dual_row(ui::Canvas& canvas, int y, const char* label, Quantity aeronautical, Quantity metric,
              bool plus_sign) {
    char text[16];
    canvas.draw_text(kLeft, y, label, true, 1);

    int written = format_signed(text, aeronautical.value, 1, aeronautical.decimals, plus_sign);
    text[written] = 0;
    right_aligned(canvas, kAeronauticalNumberEnd, y, text, written);
    canvas.draw_text(kAeronauticalUnitX, y, aeronautical.unit, true, 1);

    written = format_signed(text, metric.value, 1, metric.decimals, plus_sign);
    text[written] = 0;
    right_aligned(canvas, kMetricNumberEnd, y, text, written);
    canvas.draw_text(kMetricUnitX, y, metric.unit, true, 1);
}

// What the sensor reads, to the tenth of a pascal it measures in.
void pressure_row(ui::Canvas& canvas, int y, uint32_t pressure_mpa) {
    char barometer[12];
    const int written =
        format_unsigned(barometer, divide_rounded<uint32_t>(pressure_mpa, 100), 1, 3);
    barometer[written] = 0;

    canvas.draw_text(kLeft, y, "BARO", true, 1);
    right_aligned(canvas, kAeronauticalUnitEnd, y, barometer, written);
    canvas.draw_text(kMetricUnitX, y, " hPa", true, 1);
}

uint32_t at_most(uint32_t value, uint32_t ceiling) { return value > ceiling ? ceiling : value; }

int imu_sensor_error(char* out, const StatusSnapshot& snapshot) {
    int written = format_string(out, " SE");
    written += format_unsigned(out + written, snapshot.imu_errored_sensor, 1);
    out[written++] = ':';
    written += format_hex(out + written, snapshot.imu_sensor_error, 2);
    return written;
}

int imu_traffic(char* out, const StatusSnapshot& snapshot) {
    if (snapshot.imu_sensor_error != 0) return imu_sensor_error(out, snapshot);

    const bool stuck = snapshot.imu_unparsed != 0;
    int written = format_string(out, stuck ? " U" : " B");
    written += format_unsigned(
        out + written,
        at_most(stuck ? snapshot.imu_unparsed : snapshot.imu_fifo_bytes, kImuCountCeiling), 1);
    if (snapshot.imu_error != 0) {
        written += format_string(out + written, " E");
        written += format_hex(out + written, snapshot.imu_error, 2);
    } else if (snapshot.imu_meta_event != kImuMetaEventInitialised) {
        written += format_string(out + written, " M");
        written += format_unsigned(out + written, snapshot.imu_meta_event, 1);
    } else {
        written += format_string(out + written, " I");
        written += format_hex(out + written, snapshot.imu_interrupt, 2);
    }
    return written;
}

void imu_field(ui::Canvas& canvas, int y, const StatusSnapshot& snapshot) {
    char text[24];
    int written = format_string(text, "IMU ");
    written += format_string(text + written, snapshot.imu_stage);
    if (snapshot.imu_fault != nullptr && snapshot.imu_fault[0] != 0) {
        written += format_string(text + written, " ");
        written += format_string(text + written, snapshot.imu_fault);
    } else if (snapshot.slip_valid) {
        written += format_string(text + written, " ");
        written += format_signed(text + written, snapshot.slip_mg, 1, 0, true);
        written += format_string(text + written, "mg");
    } else {
        written += imu_traffic(text + written, snapshot);
    }
    text[written] = 0;
    right_aligned(canvas, kGlassWidth - kLeft, y, text, written);
}

// INFO: fc 05oct26 on the cable the divider reads the USB rail, so CHG stands in for the numbers
void battery_row(ui::Canvas& canvas, int y, const StatusSnapshot& snapshot) {
    if (!snapshot.battery_valid && !snapshot.charging) {
        row(canvas, y, "BAT", "no sensor");
        return;
    }

    // Centivolts, so the two decimals a cell is judged on fit the value field.
    char volts[8] = "--";
    char percent[8] = "--";
    if (snapshot.battery_valid) {
        int written = format_unsigned(volts, snapshot.battery_mv, 1, 3);
        volts[written] = 0;
        written = format_unsigned(percent, snapshot.battery_percent, 1);
        percent[written] = 0;
    }

    // A cell on the cable is not low whatever it reads, so the charger wins the
    // marker. Off it, the warning is the whole reason this row is on the page.
    const char* mark = snapshot.charge == power::ChargeCondition::TooHot    ? "% HOT"
                       : snapshot.charge == power::ChargeCondition::TooCold ? "% CLD"
                       : snapshot.charging                                  ? "% CHG"
                       : snapshot.battery_low                               ? "% LOW"
                                                                            : "%";
    text_row(canvas, y, "BAT", volts, " V", percent, mark);
}
}  // namespace

void draw_status(ui::Canvas& canvas, const StatusSnapshot& snapshot) {
    canvas.clear(true);

    char text[32];

    // Header: device address (the ADS-L identity) + the name the pilot gave it.
    int written = format_string(text, "ID ");
    written += format_air_address(text + written, snapshot.address);
    text[written] = 0;
    canvas.draw_text(kLeft, 3, text, true, 2);
    if (snapshot.callsign != nullptr && snapshot.callsign[0] != 0)
        right_aligned(canvas, kGlassWidth - kLeft, 8, snapshot.callsign, length(snapshot.callsign));
    canvas.horizontal_line(kLeft, 21, kGlassWidth - 2 * kLeft, true);

    // Everything that is one free-form string first, then the block where every
    // number lines up in its column.
    int y = 27;

    written = format_string(text, " ");
    if (snapshot.utc_valid)
        written += format_seconds_of_day(text + written, snapshot.utc_s);
    else
        written += format_string(text + written, "--:--:--");
    text[written] = 0;

    if (snapshot.fix_valid) {
        char satellites[4];
        written = format_unsigned(satellites, snapshot.satellites, 2);
        satellites[written] = 0;
        text_row(canvas, y, "GNSS", satellites, " SAT", "UTC", text);
        canvas.draw_text(kValueX, y, solution_dimensions(snapshot), true, 1);
    } else {
        char stage[16];
        written = format_string(stage, gnss::stage_name(snapshot.stage));
        stage[written++] = ' ';
        written += format_elapsed(stage + written, snapshot.stage_s);
        stage[written] = 0;
        text_row(canvas, y, "GNSS", "", "", "UTC", text);
        canvas.draw_text(kValueX, y, stage, true, 1);
    }
    y += kLineHeight;

    // Full 1e-7 degrees on both, which is what the fix carries. A signed
    // three-digit longitude is then 16 cells wide with its label, so the
    // longitude block is anchored to the right margin rather than to a column:
    // at seven decimals nothing narrower fits every position on earth.
    if (snapshot.fix_valid) {
        // Ten characters of latitude do not fit a five-cell value field, so it
        // ends where the first column's unit does: level with TRUE above it.
        written = format_signed(text, snapshot.latitude_1e7, 1, 7, false);
        text[written] = 0;
        canvas.draw_text(kLeft, y, "LAT", true, 1);
        right_aligned(canvas, kAeronauticalUnitEnd, y, text, written);

        written = format_string(text, "LON ");
        written += format_signed(text + written, snapshot.longitude_1e7, 1, 7, false);
        text[written] = 0;
        right_aligned(canvas, kGlassWidth - kLeft, y, text, written);
    } else {
        row(canvas, y, "LAT", "no fix");
    }
    y += kLineHeight;

    written = format_unsigned(text, static_cast<uint32_t>(snapshot.track_cdeg), 5, 2);
    text[written] = 0;
    text_row(canvas, y, "TRK", text, " TRUE");
    imu_field(canvas, y, snapshot);
    y += kLineHeight;

    char count[8];
    written = format_unsigned(count, static_cast<uint32_t>(snapshot.heard_count), 1);
    count[written] = 0;
    text_row(canvas, y, "TFC", count, "", "TX", snapshot.transmitting ? " ON" : " OFF");
    y += kLineHeight;

    // The aligned block: the pressure the altitudes depend on, the geometric
    // altitude, and pressure altitude on the 1013.25 hPa standard setting - the
    // one a flight level counts in hundreds of feet. Then the motion pair.
    if (snapshot.barometer_valid)
        pressure_row(canvas, y, snapshot.pressure_mpa);
    else
        row(canvas, y, "BARO", "no sensor");
    y += kLineHeight;

    dual_row(canvas, y, "GNSS", {to_feet(Millimetres(snapshot.altitude_mm)).value, 0, " ft"},
             {divide_rounded(snapshot.altitude_mm, 100), 1, " m"}, false);
    y += kLineHeight;

    if (snapshot.barometer_valid)
        dual_row(canvas, y, "STD",
                 {to_feet(Millimetres(snapshot.altitude_standard_mm)).value, 0, " ft"},
                 {divide_rounded(snapshot.altitude_standard_mm, 10), 2, " m"}, false);
    y += kLineHeight;

    dual_row(canvas, y, "SPD", {knots(snapshot.speed_mm_s), 0, " kt"},
             {centi_kilometres_per_hour(snapshot.speed_mm_s), 2, " km/h"}, false);
    y += kLineHeight;

    dual_row(canvas, y, "VS",
             {to_feet_per_minute(MillimetresPerSecond(snapshot.climb_mm_s)).value, 0, " fpm"},
             {snapshot.climb_mm_s, 3, " m/s"}, true);
    y += kLineHeight;

    battery_row(canvas, y, snapshot);
}

}  // namespace skyblip::go
