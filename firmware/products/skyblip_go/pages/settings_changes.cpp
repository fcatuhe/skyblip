#include "products/skyblip_go/pages/settings_changes.h"

#include <cstring>

#include "core/util/format.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/pages/menu.h"

namespace skyblip::go {

namespace {

constexpr int kMaxChanges = 7;

struct Rows {
    char text[kMaxChanges][kConfirmDetailCols + 1]{};
    int n{0};

    char* next() { return text[n++]; }
};

void menu_row(Rows& rows, MenuRow row, const Settings& to) {
    char* out = rows.next();
    int n = fmt_string(out, menu_row_label(row));
    out[n++] = ' ';
    MenuValues values;
    values.settings = to;
    menu_row_value(out + n, row, values);
}

void callsign_row(Rows& rows, const Settings& to) {
    char* out = rows.next();
    int n = fmt_string(out, menu_row_label(MenuRow::Callsign));
    out[n++] = ' ';
    n += fmt_string(out + n, to.callsign[0] != 0 ? to.callsign : "NONE");
    out[n] = 0;
}

void signed_row(Rows& rows, const char* label, int value, uint8_t dec_point, const char* unit) {
    char* out = rows.next();
    int n = fmt_string(out, label);
    out[n++] = ' ';
    n += fmt_int(out + n, value, 1, dec_point);
    n += fmt_string(out + n, unit);
    out[n] = 0;
}

int join(const Rows& rows, char* out) {
    const int shown = rows.n > kConfirmDetailRows ? kConfirmDetailRows - 1 : rows.n;
    int n = 0;
    for (int i = 0; i < shown; i++) {
        if (i > 0) out[n++] = '\n';
        n += fmt_string(out + n, rows.text[i]);
    }
    if (shown < rows.n) {
        n += fmt_string(out + n, "\nAND ");
        n += fmt_uint(out + n, static_cast<uint32_t>(rows.n - shown));
        n += fmt_string(out + n, " MORE");
    }
    out[n] = 0;
    return n;
}

}  // namespace

int describe_settings_changes(const Settings& from, const Settings& to, char* out, int cap) {
    if (cap < kSettingsChangesCap) return 0;
    Rows rows;
    if (to.aircraft_type != from.aircraft_type) menu_row(rows, MenuRow::AircraftType, to);
    if (std::strncmp(to.callsign, from.callsign, kCallsignCap) != 0) callsign_row(rows, to);
    if (to.units != from.units) menu_row(rows, MenuRow::Units, to);
    if (to.alarm_enabled != from.alarm_enabled) menu_row(rows, MenuRow::Alarm, to);
    if (to.alarm_volume != from.alarm_volume) menu_row(rows, MenuRow::Volume, to);
    if (to.battery_offset_mv != from.battery_offset_mv)
        signed_row(rows, "BATTERY", to.battery_offset_mv, 0, " MV");
    if (to.freq_trim_e1_ppm != from.freq_trim_e1_ppm)
        signed_row(rows, "FREQ TRIM", to.freq_trim_e1_ppm, 1, " PPM");
    if (rows.n == 0) return 0;
    return join(rows, out);
}

}  // namespace skyblip::go
