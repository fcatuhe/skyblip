#include "products/skyblip_go/pages/menu.h"

#include "core/util/format.h"

namespace skyblip::go {

namespace {

constexpr int kHeaderY = 3;
constexpr int kHeaderRuleY = 21;

constexpr MenuRow kRadarMenuRows[] = {MenuRow::Light,  MenuRow::Aircraft, MenuRow::Units,
                                      MenuRow::Range,  MenuRow::Plot,     MenuRow::Alarm,
                                      MenuRow::Volume, MenuRow::Bravo};
constexpr MenuRow kAircraftMenuRows[] = {MenuRow::Detected, MenuRow::AircraftType,
                                         MenuRow::Callsign, MenuRow::OwnIcao};
constexpr MenuRow kNearbyMenuRows[] = {MenuRow::Status, MenuRow::Satellites, MenuRow::RadioLog,
                                       MenuRow::Raw,    MenuRow::Capture,    MenuRow::SelfTest};

template <int Count>
constexpr Menu menu_of(const MenuRow (&rows)[Count]) {
    return Menu{rows, Count};
}

// CALLSIGN at double height is eight cells, and the row holds fifteen.
constexpr int kCallsignRowCharacters = 6;

const char* alarm_volume_name(go::AlarmVolume volume) {
    switch (volume) {
        case go::AlarmVolume::Off: return "OFF";
        case go::AlarmVolume::Soft: return "SOFT";
        default: return "LOUD";
    }
}

void row_text(ui::Canvas& canvas, int line, const char* label, const char* value, bool focused) {
    const int top = menu_line_top(line);
    const int y = menu_line_text_y(line);
    if (focused) canvas.rectangle(2, top, kGlassWidth - 4, kMenuBarHeight, true, /*fill=*/true);
    const bool ink = !focused;
    canvas.draw_text(kMenuLeftX, y, label, ink, kMenuScale);
    canvas.draw_text(kMenuRightX - text_cells(value) * kMenuCellWidth, y, value, ink, kMenuScale);
}

int callsign_value(char* out, const char* callsign) {
    if (callsign[0] == 0) return format_string(out, "NONE");
    int written = 0;
    while (written < kCallsignRowCharacters && callsign[written] != 0) {
        out[written] = callsign[written];
        written++;
    }
    return written;
}

void field_header(ui::Canvas& canvas, const char* title) {
    canvas.clear(true);
    canvas.draw_text(kMenuLeftX - 2, kHeaderY, title, true, kMenuScale);
    canvas.horizontal_line(kMenuLeftX - 2, kHeaderRuleY, kGlassWidth - 2 * (kMenuLeftX - 2), true);
}

}  // namespace

Menu menu_for(Page page) {
    switch (page) {
        case Page::Radar: return menu_of(kRadarMenuRows);
        case Page::Nearby: return menu_of(kNearbyMenuRows);
        default: return Menu{};
    }
}

Menu aircraft_menu() { return menu_of(kAircraftMenuRows); }

int menu_row_index(const Menu& menu, MenuRow row) {
    for (int i = 0; i < menu.row_count; i++)
        if (menu.rows[i] == row) return i;
    return -1;
}

int menu_first_row(const Menu& menu, MenuRow focus) {
    const int last_line = kMenuLines - 1;
    const int focused = menu_row_index(menu, focus);
    return focused > last_line ? focused - last_line : 0;
}

const char* menu_row_label(MenuRow row) {
    switch (row) {
        case MenuRow::Light: return "LIGHT";
        case MenuRow::AircraftType: return "TYPE";
        case MenuRow::Callsign: return "CALLSIGN";
        case MenuRow::OwnIcao: return "OWN ICAO";
        case MenuRow::Units: return "UNITS";
        case MenuRow::Range: return "RANGE";
        case MenuRow::Plot: return "PLOT";
        case MenuRow::Alarm: return "ALARM";
        case MenuRow::Volume: return "VOLUME";
        case MenuRow::Status: return "STATUS";
        case MenuRow::Satellites: return "SATELLITES";
        case MenuRow::RadioLog: return "RADIO LOG";
        case MenuRow::Raw: return "RAW";
        case MenuRow::Capture: return "CAPTURE";
        case MenuRow::SelfTest: return "SELF TEST";
        case MenuRow::Bravo: return "BRAVO";
        case MenuRow::Aircraft: return "AIRCRAFT";
        default: return "";
    }
}

Page page_behind(MenuRow row) {
    switch (row) {
        case MenuRow::RadioLog: return Page::RadioLog;
        case MenuRow::Raw: return Page::Raw;
        case MenuRow::Capture: return Page::Capture;
        case MenuRow::Satellites: return Page::Satellites;
        case MenuRow::Status: return Page::Status;
        case MenuRow::SelfTest: return Page::SelfTest;
        case MenuRow::Bravo: return Page::Bravo;
        default: return Page::Radar;
    }
}

bool named_aircraft_type(uint8_t code) { return aircraft_type_name(code)[0] != 0; }

const char* aircraft_type_name(uint8_t code) {
    for (const AircraftType& type : kAircraftTypes)
        if (type.code == code) return type.name;
    return "";
}

uint8_t next_aircraft_type(uint8_t code) {
    for (int i = 0; i + 1 < kNamedAircraftTypes; i++)
        if (kAircraftTypes[i].code == code) return kAircraftTypes[i + 1].code;
    return kAircraftTypes[0].code;
}

char next_callsign_character(char character) {
    if (character == kCallsignBlank) return '-';
    if (character == '-') return 'A';
    if (character >= 'A' && character < 'Z') return static_cast<char>(character + 1);
    if (character == 'Z') return '0';
    if (character >= '0' && character < '9') return static_cast<char>(character + 1);
    return kCallsignBlank;
}

bool callsign_press_clears(const char* text, int cursor) {
    return cursor == 0 && text[0] == kCallsignBlank;
}

char next_hex_character(char character) {
    if (character == kCallsignBlank) return '0';
    if (character >= '0' && character < '9') return static_cast<char>(character + 1);
    if (character == '9') return 'A';
    if (character >= 'A' && character < 'F') return static_cast<char>(character + 1);
    return kCallsignBlank;
}

uint32_t own_icao_typed(const char* edited) {
    uint32_t address = 0;
    for (int i = 0; i < kOwnIcaoCharacters; i++) {
        const char digit = edited[i];
        uint32_t nibble = 0;
        if (digit >= '0' && digit <= '9')
            nibble = static_cast<uint32_t>(digit - '0');
        else if (digit >= 'A' && digit <= 'F')
            nibble = static_cast<uint32_t>(digit - 'A' + 10);
        else
            return kOwnIcaoUnset;
        address = (address << 4) | nibble;
    }
    return address;
}

int callsign_stored(char* out, const char* edited) {
    int written = 0;
    while (written < kCallsignCharacters && edited[written] != 0) {
        out[written] = edited[written];
        written++;
    }
    while (written > 0 && out[written - 1] == kCallsignBlank) written--;
    out[written] = 0;
    return written;
}

int menu_row_value(char* out, MenuRow row, const MenuValues& values) {
    int written = 0;
    switch (row) {
        case MenuRow::AircraftType: {
            const char* name = aircraft_type_name(values.settings.aircraft_type);
            if (name[0] != 0) {
                written = format_string(out, name);
            } else {
                written = format_string(out, "CODE ");
                written += format_unsigned(out + written, values.settings.aircraft_type);
            }
            break;
        }
        // The row has the label beside it, so it shows what a registration
        // is identified by and the field itself shows all nine.
        case MenuRow::Aircraft:
        case MenuRow::Callsign: written = callsign_value(out, values.settings.callsign); break;
        case MenuRow::OwnIcao:
            written = values.settings.own_icao == kOwnIcaoUnset
                          ? format_string(out, "NONE")
                          : format_hex(out, values.settings.own_icao, kOwnIcaoCharacters);
            break;
        case MenuRow::Units:
            written = format_string(
                out, values.settings.units == go::Units::Metric ? "METRIC" : "NAUTICAL");
            break;
        case MenuRow::Range:
            written = format_unsigned(out, static_cast<uint32_t>(range_value(
                                               values.settings.range_step, values.settings.units)));
            written += format_string(out + written, " ");
            written += format_string(out + written, range_unit(values.settings.units));
            break;
        case MenuRow::Plot:
            written =
                format_string(out, values.settings.plot == RadarPlot::All ? "ALL" : "TO SCALE");
            break;
        case MenuRow::Light: written = format_string(out, values.light ? "ON" : "OFF"); break;
        case MenuRow::Bravo:
            written = paired(values.settings)
                          ? format_hex(out, events::bluetooth_tag(values.settings.bravo), 4)
                          : format_string(out, "NONE");
            break;
        case MenuRow::Alarm:
            written = format_string(out, values.settings.alarm_enabled ? "ON" : "OFF");
            break;
        case MenuRow::Volume:
            written = format_string(out, alarm_volume_name(values.settings.alarm_volume));
            break;
        default: break;
    }
    out[written] = 0;
    return written;
}

// INFO: fc 13nov26 what Bravo heard takes four lines, and the three rows it sets fit under it
void draw_aircraft_menu(ui::Canvas& canvas, const MenuSnapshot& snapshot) {
    const Menu menu = aircraft_menu();
    draw_detected(canvas, snapshot.values.detected, snapshot.values.settings,
                  snapshot.focus == MenuRow::Detected);
    char value[kMenuValueBytes];
    for (int i = 1; i < menu.row_count; i++) {
        const MenuRow row = menu.rows[i];
        menu_row_value(value, row, snapshot.values);
        row_text(canvas, kDetectedLines + i - 1, menu_row_label(row), value, row == snapshot.focus);
    }
}

void draw_menu(ui::Canvas& canvas, const MenuSnapshot& snapshot) {
    canvas.clear(true);
    const char* title =
        snapshot.aircraft ? menu_row_label(MenuRow::Aircraft) : menu_title(snapshot.page);
    canvas.draw_text(kMenuLeftX - 2, kHeaderY, title, true, kMenuScale);
    canvas.horizontal_line(kMenuLeftX - 2, kHeaderRuleY, kGlassWidth - 2 * (kMenuLeftX - 2), true);

    if (snapshot.aircraft) {
        draw_aircraft_menu(canvas, snapshot);
    } else {
        const Menu menu = menu_for(snapshot.page);
        const int first = menu_first_row(menu, snapshot.focus);
        char value[kMenuValueBytes];
        for (int line = 0; line < kMenuLines && first + line < menu.row_count; line++) {
            const MenuRow row = menu.rows[first + line];
            menu_row_value(value, row, snapshot.values);
            row_text(canvas, line, menu_row_label(row), value, row == snapshot.focus);
        }
    }

    canvas.draw_text(kMenuHintX, kMenuHintY, kMenuHintText, true, 1);
}

void draw_callsign(ui::Canvas& canvas, const CallsignSnapshot& snapshot) {
    field_header(canvas, snapshot.title);

    const int text_x = field_text_x(text_cells(snapshot.text));
    canvas.draw_text(text_x, kCallsignTextY, snapshot.text, true, kCallsignScale);
    const int at = text_x + snapshot.cursor * kCallsignCellWidth;
    canvas.rectangle(at, kCallsignCursorY, 5 * kCallsignScale, kCallsignCursorHeight, true,
                     /*fill=*/true);

    const char* button = snapshot.clears ? kCallsignClearHintText : kCallsignNextHintText;
    canvas.draw_text(centred_x(kCallsignPadHintText, kCallsignHintScale), kCallsignPadHintY,
                     kCallsignPadHintText, true, kCallsignHintScale);
    canvas.draw_text(centred_x(button, kCallsignHintScale), kCallsignButtonHintY, button, true,
                     kCallsignHintScale);
    if (!snapshot.clears)
        canvas.draw_text(centred_x(kCallsignClearHelpText, 1), kCallsignHelpY,
                         kCallsignClearHelpText, true, 1);
}

void draw_clear(ui::Canvas& canvas, const ClearSnapshot& snapshot) {
    field_header(canvas, snapshot.title);
    canvas.draw_text(field_text_x(text_cells(snapshot.text)), kCallsignTextY, snapshot.text, true,
                     kCallsignScale);
    canvas.draw_text(centred_x(kClearKeepsHintText, kCallsignHintScale), kCallsignPadHintY,
                     kClearKeepsHintText, true, kCallsignHintScale);
    canvas.draw_text(centred_x(kCallsignClearHintText, kCallsignHintScale), kCallsignButtonHintY,
                     kCallsignClearHintText, true, kCallsignHintScale);
    canvas.draw_text(centred_x(kOwnIcaoClearHelpText, 1), kCallsignHelpY, kOwnIcaoClearHelpText,
                     true, 1);
}

}  // namespace skyblip::go
