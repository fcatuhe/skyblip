#include "core/util/format.h"
#include "products/skyblip_go/pages/menu.h"

namespace skyblip::go {

void MenuEditor::enter(Page page, uint32_t now_ms) {
    page_ = page;
    focus_ = 0;
    aircraft_ = false;
    idle_since_ms_ = now_ms;
    active_ = menu_for(page).row_count > 0;
    pending_ = Pending::None;
    close_field();
}

void MenuEditor::leave() {
    focus_ = 0;
    aircraft_ = false;
    active_ = false;
    pending_ = Pending::None;
    close_field();
}

void MenuEditor::close_field() {
    editing_ = false;
    clearing_ = false;
    refused_ = false;
    cursor_ = 0;
}

Menu MenuEditor::menu() const { return aircraft_ ? aircraft_menu() : menu_for(page_); }

MenuRow MenuEditor::focus() const {
    const Menu menu = this->menu();
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
    if (focus_ + 1 < menu().row_count) {
        focus_++;
        return MenuAction::Moved;
    }
    if (aircraft_) {
        back_to_settings();
        return MenuAction::Moved;
    }
    leave();
    return MenuAction::Leave;
}

// INFO: fc 13nov26 the thumb comes back to the row it left from, which now reads what was stored
void MenuEditor::back_to_settings() {
    aircraft_ = false;
    focus_ = menu_row_index(menu_for(page_), MenuRow::Aircraft);
}

MenuAction MenuEditor::use_detected(const MenuValues& current, MenuValues& next) {
    if (current.detected.state == Detection::Unpaired) {
        opening_ = Page::Bravo;
        leave();
        return MenuAction::Open;
    }
    if (!adopt(current.detected, next.settings)) return MenuAction::None;
    back_to_settings();
    return MenuAction::Changed;
}

// Full width whatever is stored, so every position can be rolled and the ones a
// pilot leaves blank are what the stored form drops.
void MenuEditor::edit(MenuRow field, const MenuValues& current) {
    field_ = field;
    editing_ = true;
    clearing_ = false;
    refused_ = false;
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
    refused_ = false;
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
        // INFO: fc 14nov26 ADS-L F.2.3: refused in the open field, under what Bravo names instead
        if (contradicts(next.settings.own_icao, current.own_transponder)) {
            next = current;
            refused_ = true;
            cursor_ = 0;
            return MenuAction::Moved;
        }
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
        case MenuRow::Aircraft:
            aircraft_ = true;
            focus_ = 0;
            return MenuAction::Moved;
        case MenuRow::Detected: return use_detected(current, next);
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
