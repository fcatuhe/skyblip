#include "products/skyblip_go/pages/menu.h"

#include "core/util/format.h"

namespace skyblip::go {

namespace {

constexpr int kHeaderY = 3;
constexpr int kHeaderRuleY = 21;

constexpr MenuRow kRadarMenuRows[] = {
    MenuRow::Light, MenuRow::AircraftType, MenuRow::Callsign, MenuRow::Units,   MenuRow::Range,
    MenuRow::Plot,  MenuRow::Alarm,        MenuRow::Volume,   MenuRow::OwnIcao, MenuRow::Bravo};
constexpr MenuRow kNearbyMenuRows[] = {MenuRow::Status, MenuRow::Satellites, MenuRow::RadioLog,
                                       MenuRow::Raw,    MenuRow::Capture,    MenuRow::SelfTest};

template <int Count>
constexpr Menu menu_of(const MenuRow (&rows)[Count]) {
    return Menu{rows, Count};
}

// INFO: cf 02aug26 ADS-L 4 SRD-860 issue 2 G.1.3 wire values: the code stored is transmitted
const char* const kAircraftNames[kNamedAircraftTypes] = {
    "UNKNOWN",    "LIGHT",      "HEAVY",    "HELICOPTER", "GLIDER",    "BALLOON",
    "MICROLIGHT", "PARAGLIDER", "SKYDIVER", "VTOL",       "GYROCOPTER"};

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

const char* aircraft_type_name(uint8_t code) {
    return code < kNamedAircraftTypes ? kAircraftNames[code] : "";
}

uint8_t next_aircraft_type(uint8_t code) {
    if (code + 1 >= kNamedAircraftTypes) return 0;
    return static_cast<uint8_t>(code + 1);
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
        case MenuRow::Callsign: {
            if (values.settings.callsign[0] == 0) {
                written = format_string(out, "NONE");
                break;
            }
            // The row has the label beside it, so it shows what a registration
            // is identified by and the field itself shows all nine.
            while (written < kCallsignRowCharacters && values.settings.callsign[written] != 0) {
                out[written] = values.settings.callsign[written];
                written++;
            }
            break;
        }
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

void draw_menu(ui::Canvas& canvas, const MenuSnapshot& snapshot) {
    canvas.clear(true);
    canvas.draw_text(kMenuLeftX - 2, kHeaderY, menu_title(snapshot.page), true, kMenuScale);
    canvas.horizontal_line(kMenuLeftX - 2, kHeaderRuleY, kGlassWidth - 2 * (kMenuLeftX - 2), true);

    const Menu menu = menu_for(snapshot.page);
    const int first = menu_first_row(menu, snapshot.focus);
    char value[kMenuValueBytes];
    for (int line = 0; line < kMenuLines && first + line < menu.row_count; line++) {
        const MenuRow row = menu.rows[first + line];
        menu_row_value(value, row, snapshot.values);
        row_text(canvas, line, menu_row_label(row), value, row == snapshot.focus);
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

void MenuEditor::enter(Page page, uint32_t now_ms) {
    page_ = page;
    focus_ = 0;
    idle_since_ms_ = now_ms;
    active_ = menu_for(page).row_count > 0;
    pending_ = Pending::None;
    close_field();
}

void MenuEditor::leave() {
    focus_ = 0;
    active_ = false;
    pending_ = Pending::None;
    close_field();
}

void MenuEditor::close_field() {
    editing_ = false;
    clearing_ = false;
    cursor_ = 0;
}

MenuRow MenuEditor::focus() const {
    const Menu menu = menu_for(page_);
    if (focus_ < 0 || focus_ >= menu.row_count) return MenuRow::kCount;
    return menu.rows[focus_];
}

void MenuEditor::button(uint32_t now_ms) {
    if (!active_) return;
    idle_since_ms_ = now_ms;
    pending_ = Pending::Act;
}

void MenuEditor::pad(uint32_t now_ms) {
    if (!active_) return;
    idle_since_ms_ = now_ms;
    pending_ = Pending::Advance;
}

MenuAction MenuEditor::tick(uint32_t now_ms, const MenuValues& current, MenuValues& next) {
    if (!active_) return MenuAction::None;
    const Pending pending = pending_;
    pending_ = Pending::None;
    if (pending == Pending::Act) {
        if (clearing_) return confirm_clear(current, next);
        return editing_ ? step(current, next) : act(current, next);
    }
    if (pending == Pending::Advance) {
        if (clearing_) return keep();
        return editing_ ? roll() : advance();
    }
    if (now_ms - idle_since_ms_ >= kIdleReturnHoldMs) {
        leave();
        return MenuAction::Leave;
    }
    return MenuAction::None;
}

MenuAction MenuEditor::advance() {
    if (focus_ + 1 >= menu_for(page_).row_count) {
        leave();
        return MenuAction::Leave;
    }
    focus_++;
    return MenuAction::Moved;
}

// Full width whatever is stored, so every position can be rolled and the ones a
// pilot leaves blank are what the stored form drops.
void MenuEditor::edit(MenuRow field, const MenuValues& current) {
    field_ = field;
    editing_ = true;
    clearing_ = false;
    cursor_ = 0;
    int written = 0;
    if (field == MenuRow::OwnIcao) {
        if (current.settings.own_icao != kOwnIcaoUnset)
            written = format_hex(text_, current.settings.own_icao, kOwnIcaoCharacters);
    } else {
        while (written < kCallsignCharacters && current.settings.callsign[written] != 0) {
            text_[written] = current.settings.callsign[written];
            written++;
        }
    }
    while (written < characters()) text_[written++] = kCallsignBlank;
    text_[characters()] = 0;
}

int MenuEditor::characters() const {
    return field_ == MenuRow::OwnIcao ? kOwnIcaoCharacters : kCallsignCharacters;
}

MenuAction MenuEditor::roll() {
    text_[cursor_] = field_ == MenuRow::OwnIcao ? next_hex_character(text_[cursor_])
                                                : next_callsign_character(text_[cursor_]);
    return MenuAction::Moved;
}

// The last character's button press is what stores the field: a field a pilot
// walks off the end of is a field they have finished with.
MenuAction MenuEditor::step(const MenuValues& current, MenuValues& next) {
    if (callsign_press_clears(text_, cursor_)) return clear(current, next);
    if (cursor_ + 1 < characters()) {
        cursor_++;
        return MenuAction::Moved;
    }
    return store(current, next);
}

MenuAction MenuEditor::store(const MenuValues& current, MenuValues& next) {
    next = current;
    bool typed = true;
    if (field_ == MenuRow::OwnIcao) {
        next.settings.own_icao = own_icao_typed(text_);
        typed = next.settings.own_icao != kOwnIcaoUnset;
    } else {
        callsign_stored(next.settings.callsign, text_);
    }
    close_field();
    if (typed && go::validate(next.settings) == Status::Ok) return MenuAction::Changed;
    next = current;
    return MenuAction::Moved;
}

MenuAction MenuEditor::clear(const MenuValues& current, MenuValues& next) {
    if (field_ == MenuRow::OwnIcao) {
        if (current.settings.own_icao == kOwnIcaoUnset) {
            close_field();
            return MenuAction::Moved;
        }
        format_hex(text_, current.settings.own_icao, kOwnIcaoCharacters);
        text_[kOwnIcaoCharacters] = 0;
        clearing_ = true;
        return MenuAction::Moved;
    }
    close_field();
    if (current.settings.callsign[0] == 0) return MenuAction::Moved;
    next = current;
    next.settings.callsign[0] = 0;
    return MenuAction::Changed;
}

MenuAction MenuEditor::confirm_clear(const MenuValues& current, MenuValues& next) {
    close_field();
    next = current;
    next.settings.own_icao = kOwnIcaoUnset;
    return MenuAction::Changed;
}

MenuAction MenuEditor::keep() {
    close_field();
    return MenuAction::Moved;
}

// INFO: cf 02aug26 one gate for every accepted value, go::validate, the same one a phone goes past
MenuAction MenuEditor::act(const MenuValues& current, MenuValues& next) {
    next = current;
    const MenuRow row = focus();
    if (opens_a_page(row)) {
        opening_ = page_behind(row);
        leave();
        return MenuAction::Open;
    }
    switch (row) {
        case MenuRow::Light:
            next.light = !current.light;
            leave();
            return MenuAction::Light;
        case MenuRow::Callsign:
        case MenuRow::OwnIcao: edit(row, current); return MenuAction::Moved;
        case MenuRow::AircraftType:
            next.settings.aircraft_type = next_aircraft_type(current.settings.aircraft_type);
            break;
        case MenuRow::Units:
            next.settings.units = current.settings.units == go::Units::Metric ? go::Units::Nautical
                                                                              : go::Units::Metric;
            break;
        case MenuRow::Range:
            next.settings.range_step =
                static_cast<uint8_t>(next_range_step(current.settings.range_step));
            break;
        case MenuRow::Plot: next.settings.plot = next_radar_plot(current.settings.plot); break;
        case MenuRow::Alarm: next.settings.alarm_enabled = !current.settings.alarm_enabled; break;
        case MenuRow::Volume:
            next.settings.alarm_volume = next_alarm_volume(current.settings.alarm_volume);
            break;
        default: leave(); return MenuAction::Leave;
    }

    if (go::validate(next.settings) != Status::Ok) {
        next = current;
        return MenuAction::None;
    }
    return MenuAction::Changed;
}

}  // namespace skyblip::go
