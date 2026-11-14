#include "products/skyblip_go/pages/settings_changes.h"

#include <cstring>

#include "core/util/format.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/pages/menu.h"

namespace skyblip::go {

namespace {

constexpr int kMaxChanges = 8;

// INFO: fc 14nov26 a row may run to two lines on the glass, which join() counts
constexpr int kRowBytes = 2 * kConfirmDetailColumns + 2;

struct Rows {
    char text[kMaxChanges][kRowBytes]{};
    int count{0};

    char* next() { return text[count++]; }
};

void menu_row(Rows& rows, MenuRow row, const Settings& to) {
    char* out = rows.next();
    int written = format_string(out, menu_row_label(row));
    out[written++] = ' ';
    MenuValues values;
    values.settings = to;
    menu_row_value(out + written, row, values);
}

void callsign_row(Rows& rows, const Settings& to) {
    char* out = rows.next();
    int written = format_string(out, menu_row_label(MenuRow::Callsign));
    out[written++] = ' ';
    written += format_string(out + written, to.callsign[0] != 0 ? to.callsign : "NONE");
    out[written] = 0;
}

void signed_row(Rows& rows, const char* label, int value, uint8_t fraction_digits,
                const char* unit) {
    char* out = rows.next();
    int written = format_string(out, label);
    out[written++] = ' ';
    written += format_signed(out + written, value, 1, fraction_digits);
    written += format_string(out + written, unit);
    out[written] = 0;
}

int lines_of(const char* row) { return text_cells(row) > kConfirmDetailColumns ? 2 : 1; }

int join(const Rows& rows, char* out) {
    int needed = 0;
    for (int i = 0; i < rows.count; i++) needed += lines_of(rows.text[i]);
    const int room = needed > kConfirmDetailRows ? kConfirmDetailRows - 1 : kConfirmDetailRows;
    int shown = 0;
    for (int used = 0; shown < rows.count && used + lines_of(rows.text[shown]) <= room; shown++)
        used += lines_of(rows.text[shown]);
    int written = 0;
    for (int i = 0; i < shown; i++) {
        if (i > 0) out[written++] = '\n';
        written += format_string(out + written, rows.text[i]);
    }
    if (shown < rows.count) {
        if (shown > 0) out[written++] = '\n';
        written += format_string(out + written, "AND ");
        written += format_unsigned(out + written, static_cast<uint32_t>(rows.count - shown));
        written += format_string(out + written, " MORE");
    }
    out[written] = 0;
    return written;
}

}  // namespace

int describe_settings_changes(const Settings& from, const Settings& to, char* out, int capacity) {
    if (capacity < kSettingsChangesBytes) return 0;
    Rows rows;
    if (to.aircraft_type != from.aircraft_type) menu_row(rows, MenuRow::AircraftType, to);
    if (std::strncmp(to.callsign, from.callsign, kCallsignBytes) != 0) callsign_row(rows, to);
    if (to.own_icao != from.own_icao) menu_row(rows, MenuRow::OwnIcao, to);
    if (to.units != from.units) menu_row(rows, MenuRow::Units, to);
    if (to.alarm_enabled != from.alarm_enabled) menu_row(rows, MenuRow::Alarm, to);
    if (to.alarm_volume != from.alarm_volume) menu_row(rows, MenuRow::Volume, to);
    if (to.battery_offset_mv != from.battery_offset_mv)
        signed_row(rows, "BATTERY", to.battery_offset_mv, 0, " MV");
    if (to.frequency_trim_e1_ppm != from.frequency_trim_e1_ppm)
        signed_row(rows, "FREQ", to.frequency_trim_e1_ppm, 1, " PPM");
    if (rows.count == 0) return 0;
    return join(rows, out);
}

}  // namespace skyblip::go
