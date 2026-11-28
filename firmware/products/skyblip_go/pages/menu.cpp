#include "products/skyblip_go/pages/menu.h"

#include "core/util/format.h"
#include "products/skyblip_go/pages/confirm.h"

namespace skyblip::go {

namespace {

constexpr MenuRow kRadarMenuRows[] = {MenuRow::Light, MenuRow::Aircraft, MenuRow::Flarm,
                                      MenuRow::Units, MenuRow::Range,    MenuRow::Plot,
                                      MenuRow::Alarm, MenuRow::Bravo,    MenuRow::Power};
constexpr MenuRow kAircraftMenuRows[] = {MenuRow::Detected, MenuRow::AircraftType,
                                         MenuRow::Callsign, MenuRow::AircraftAddress};
constexpr MenuRow kFlarmMenuRows[] = {MenuRow::FlarmHeard, MenuRow::FlarmId};
constexpr MenuRow kNearbyMenuRows[] = {MenuRow::Status, MenuRow::Satellites, MenuRow::RadioLog,
                                       MenuRow::BravoLog, MenuRow::SelfTest};

template <int Count>
constexpr Menu menu_of(const MenuRow (&rows)[Count]) {
    return Menu{rows, Count};
}

// INFO: fc 20nov26 the row holds fifteen cells and CALLSIGN takes eight, so six of a registration
constexpr int kCallsignRowCharacters = 6;

// INFO: fc 17nov26 a plain Go's soft and loud sound on its Bravo alone, so the row says so
const char* alarm_name(const go::Settings& settings, bool sounds) {
    const go::Alarm alarm = settings.alarm;
    if (alarm == go::Alarm::Off) return "OFF";
    if (alarm == go::Alarm::Visual || !go::alarm_sounds(settings, sounds)) return "VISUAL";
    if (!sounds) return alarm == go::Alarm::Soft ? "BRAVO SOFT" : "BRAVO LOUD";
    return alarm == go::Alarm::Soft ? "SOFT" : "LOUD";
}

void row_text(ui::Canvas& canvas, int line, const char* label, const char* value, bool focused) {
    const int top = menu_line_top(line);
    const int y = menu_line_text_y(line);
    if (focused)
        canvas.rounded_rectangle(2, top, kGlassWidth - 4, kMenuBarHeight, kMenuBarRadius, true,
                                 /*fill=*/true);
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

const char* detected_verb(const Detected& detected, const Settings& settings) {
    if (detected.state == Detection::Unpaired) return "OPEN";
    return detected.state == Detection::Heard && !matches(detected, settings) ? "SET" : "";
}

const char* row_verb(MenuRow row, const MenuValues& values) {
    if (row == MenuRow::Detected) return detected_verb(values.detected, values.settings);
    if (row == MenuRow::FlarmHeard)
        return values.flarm.state == FlarmDetection::Heard &&
                       !flarm_matches(values.flarm, values.settings)
                   ? "SET"
                   : "";
    return opens_a_page(row) || row == MenuRow::Aircraft || row == MenuRow::Flarm ? "OPEN"
                                                                                  : "CHANGE";
}

int hex_value(char* out, uint32_t value) {
    return value == kAircraftAddressUnset ? format_string(out, "NONE")
                                          : format_hex(out, value, kAircraftAddressCharacters);
}

// INFO: fc 14nov26 under the digits, at the size the field is read at, the refusal first
void draw_heard(ui::Canvas& canvas, uint32_t address, const char* heard_text, bool refused) {
    char digits[kAircraftAddressCharacters + 1];
    digits[format_hex(digits, address, kAircraftAddressCharacters)] = 0;
    int y = kOwnTransponderY;
    for (const char* line : {refused ? kOwnTransponderRefusedText : "", heard_text,
                             static_cast<const char*>(digits)}) {
        if (line[0] == 0) continue;
        canvas.draw_text(centred_x(line, kMenuScale), y, line, true, kMenuScale);
        y += line_pitch(kMenuScale);
    }
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

Menu flarm_menu() { return menu_of(kFlarmMenuRows); }

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
        case MenuRow::AircraftAddress: return "ADS-B";
        case MenuRow::Units: return "UNITS";
        case MenuRow::Range: return "RANGE";
        case MenuRow::Plot: return "PLOT";
        case MenuRow::Alarm: return "ALARM";
        case MenuRow::Status: return "STATUS";
        case MenuRow::Satellites: return "SATELLITES";
        case MenuRow::RadioLog: return "RADIO LOG";
        case MenuRow::BravoLog: return "BRAVO LOG";
        case MenuRow::SelfTest: return "SELF TEST";
        case MenuRow::Bravo: return "BRAVO";
        case MenuRow::Aircraft: return "AIRCRAFT";
        case MenuRow::Power: return "POWER";
        case MenuRow::Flarm: return "FLARM";
        case MenuRow::FlarmId: return kFlarmIdLabel;
        default: return "";
    }
}

Page page_behind(MenuRow row) {
    switch (row) {
        case MenuRow::RadioLog: return Page::RadioLog;
        case MenuRow::BravoLog: return Page::BravoLog;
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

uint32_t hex_typed(const char* edited) {
    uint32_t address = 0;
    for (int i = 0; i < kAircraftAddressCharacters; i++) {
        const char digit = edited[i];
        uint32_t nibble = 0;
        if (digit >= '0' && digit <= '9')
            nibble = static_cast<uint32_t>(digit - '0');
        else if (digit >= 'A' && digit <= 'F')
            nibble = static_cast<uint32_t>(digit - 'A' + 10);
        else
            return kAircraftAddressUnset;
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
        case MenuRow::Aircraft:
        case MenuRow::Callsign: written = callsign_value(out, values.settings.callsign); break;
        case MenuRow::AircraftAddress:
            written = hex_value(out, values.settings.aircraft_address);
            break;
        case MenuRow::Flarm:
        case MenuRow::FlarmId: written = hex_value(out, values.settings.flarm_id); break;
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
        case MenuRow::Power:
            written = format_string(
                out, values.settings.power_switch == power::Switch::Cable ? "CABLE" : "BUTTON");
            break;
        case MenuRow::Bravo:
            written = paired(values.settings)
                          ? format_hex(out, events::bluetooth_tag(values.settings.bravo), 4)
                          : format_string(out, "NONE");
            break;
        case MenuRow::Alarm:
            written = format_string(out, alarm_name(values.settings, values.sounds));
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

// INFO: fc 27nov26 what the Go hears aboard takes four lines, and the ID it sets fits under it
void draw_flarm_menu(ui::Canvas& canvas, const MenuSnapshot& snapshot) {
    draw_flarm_detected(canvas, snapshot.values.flarm, snapshot.values.settings,
                        snapshot.focus == MenuRow::FlarmHeard);
    char value[kMenuValueBytes];
    menu_row_value(value, MenuRow::FlarmId, snapshot.values);
    row_text(canvas, kDetectedLines, menu_row_label(MenuRow::FlarmId), value,
             snapshot.focus == MenuRow::FlarmId);
}

Menu snapshot_menu(const MenuSnapshot& snapshot) {
    if (snapshot.aircraft) return aircraft_menu();
    if (snapshot.flarm) return flarm_menu();
    return menu_for(snapshot.page);
}

Keys menu_keys(const MenuSnapshot& snapshot) {
    const Menu menu = snapshot_menu(snapshot);
    const bool last = menu_row_index(menu, snapshot.focus) == menu.row_count - 1;
    return {row_verb(snapshot.focus, snapshot.values), last ? "BACK" : "NEXT"};
}

void draw_menu(ui::Canvas& canvas, const MenuSnapshot& snapshot) {
    const char* title = snapshot.aircraft ? menu_row_label(MenuRow::Aircraft)
                        : snapshot.flarm  ? menu_row_label(MenuRow::Flarm)
                                          : menu_title(snapshot.page);
    draw_heading(canvas, title, Heading::Opened);

    if (snapshot.aircraft) {
        draw_aircraft_menu(canvas, snapshot);
    } else if (snapshot.flarm) {
        draw_flarm_menu(canvas, snapshot);
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

    draw_keys(canvas, menu_keys(snapshot));
}

const char* field_button_verb(const CallsignSnapshot& snapshot) {
    if (snapshot.clears) return kFieldEraseVerb;
    return snapshot.cursor + 1 >= text_cells(snapshot.text) ? kFieldSaveVerb : kFieldNextVerb;
}

void draw_callsign(ui::Canvas& canvas, const CallsignSnapshot& snapshot) {
    draw_heading(canvas, snapshot.title, Heading::Opened);

    const int text_x = field_text_x(text_cells(snapshot.text));
    canvas.draw_text(text_x, kCallsignTextY, snapshot.text, true, kCallsignScale);
    const int at = text_x + snapshot.cursor * kCallsignCellWidth;
    canvas.rectangle(at, kCallsignCursorY, 5 * kCallsignScale, kCallsignCursorHeight, true,
                     /*fill=*/true);

    if (snapshot.heard) draw_heard(canvas, *snapshot.heard, snapshot.heard_text, snapshot.refused);
    draw_keys(canvas, {field_button_verb(snapshot), kFieldChangeVerb});
}

void draw_clear(ui::Canvas& canvas, const ClearSnapshot& snapshot) {
    draw_heading(canvas, snapshot.title, Heading::Opened);
    const BodyLine body[] = {{snapshot.text, kCallsignScale},
                             {""},
                             {snapshot.owner},
                             {kAircraftAddressClearLines[1]},
                             {kAircraftAddressClearLines[2]}};
    draw_question_body(canvas, Heading::Opened, snapshot.seconds_left, body, 5);
    draw_keys(canvas, {kFieldEraseVerb, kFieldKeepVerb});
}

}  // namespace skyblip::go
