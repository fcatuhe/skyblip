#include "products/skyblip_go/pages/aircraft.h"

#include <cstring>

#include "core/util/format.h"
#include "products/skyblip_go/pages/menu.h"

namespace skyblip::go {

namespace {

constexpr int kStateInset = 4;
constexpr int kDetailInset = 22;
constexpr int kDetailRows = 3;
constexpr int kLabelInset = 6;
constexpr int kNameInset = 24;
constexpr int kTypeInset = 50;
constexpr int kLabelBytes = 24;

const char* state_text(Detection state) {
    switch (state) {
        case Detection::Unpaired: return "NOT PAIRED";
        case Detection::NotConnected: return "NOT CONNECTED";
        case Detection::Airborne: return "IN FLIGHT";
        case Detection::NoFix: return "NO FIX";
        case Detection::Listening: return "NOT HEARD";
        case Detection::Heard: break;
    }
    return "";
}

const char* detail_text(Detection state) {
    switch (state) {
        case Detection::Unpaired: return "PAIR A BRAVO FIRST.";
        case Detection::Airborne:
        case Detection::NoFix: return "BRAVO FINDS IT ON THE GROUND.";
        case Detection::Listening: return "SET THE TRANSPONDER TO ALT.";
        case Detection::NotConnected:
        case Detection::Heard: break;
    }
    return "";
}

bool named_type(uint8_t code) { return code != 0 && named_aircraft_type(code); }

// INFO: fc 13nov26 the address stands in for a transponder that sends no identification
const char* name_of(const Detected& detected, char* address) {
    address[format_hex(address, detected.address, kOwnIcaoCharacters)] = 0;
    return detected.callsign[0] != 0 ? detected.callsign : address;
}

void heard_label(char* out, const Detected& detected) {
    int written = format_string(out, "ICAO ");
    written += format_hex(out + written, detected.address, kOwnIcaoCharacters);
    out[written] = 0;
}

void wrapped(ui::Canvas& canvas, int y, const char* text, bool ink) {
    char line[kFrameColumns + 1];
    int at = 0;
    for (int row = 0; row < kDetailRows; row++) {
        at = take_line(text, at, line, kFrameColumns);
        if (line[0] == 0) return;
        canvas.draw_text(kMenuLeftX, y + row * line_pitch(kMenuScale), line, ink, kMenuScale);
    }
}

}  // namespace

bool adopt(const Detected& detected, Settings& settings) {
    if (detected.state != Detection::Heard) return false;
    Settings next = settings;
    next.own_icao = detected.address;
    // INFO: fc 13nov26 another aircraft's name must not stay on the air beside this one's address
    if (detected.callsign[0] != 0)
        callsign_stored(next.callsign, detected.callsign);
    else if (detected.address != settings.own_icao)
        next.callsign[0] = 0;
    if (named_type(detected.aircraft_type)) next.aircraft_type = detected.aircraft_type;
    const bool changed = next.own_icao != settings.own_icao ||
                         std::strcmp(next.callsign, settings.callsign) != 0 ||
                         next.aircraft_type != settings.aircraft_type;
    if (!changed || validate(next) != Status::Ok) return false;
    settings = next;
    return true;
}

bool matches(const Detected& detected, const Settings& settings) {
    Settings copy = settings;
    return detected.state == Detection::Heard && !adopt(detected, copy);
}

bool same_aircraft(const Detected& a, const Detected& b) {
    return a.state == b.state && a.address == b.address &&
           std::strcmp(a.callsign, b.callsign) == 0 && a.aircraft_type == b.aircraft_type;
}

void draw_detected(ui::Canvas& canvas, const Detected& detected, const Settings& settings,
                   bool focused) {
    const int top = menu_line_top(0);
    if (focused)
        canvas.rounded_rectangle(2, top, kGlassWidth - 4, kDetectedLines * kMenuRowHeight - 1,
                                 kMenuBarRadius, true, /*fill=*/true);
    const bool ink = !focused;
    if (detected.state != Detection::Heard) {
        canvas.draw_text(kMenuLeftX, top + kStateInset, state_text(detected.state), ink,
                         kMenuScale);
        wrapped(canvas, top + kDetailInset, detail_text(detected.state), ink);
        return;
    }
    char label[kLabelBytes];
    heard_label(label, detected);
    canvas.draw_text(kMenuLeftX, top + kLabelInset, label, ink, kMenuScale);
    char address[kOwnIcaoCharacters + 1];
    canvas.draw_text(kMenuLeftX, top + kNameInset, name_of(detected, address), ink, kCallsignScale);
    const char* type = matches(detected, settings) ? "SAME"
                       : named_type(detected.aircraft_type)
                           ? aircraft_type_name(detected.aircraft_type)
                           : "";
    canvas.draw_text(kMenuLeftX, top + kTypeInset, type, ink, kMenuScale);
}

void draw_aircraft_notice(ui::Canvas& canvas, const Detected& detected) {
    draw_heading(canvas, kAircraftNoticeTitle, Heading::Interrupt);
    char address[kOwnIcaoCharacters + 1];
    char label[kLabelBytes];
    heard_label(label, detected);
    BodyLine body[6];
    int count = 0;
    body[count++] = {name_of(detected, address), kCallsignScale};
    body[count++] = {label};
    if (named_type(detected.aircraft_type))
        body[count++] = {aircraft_type_name(detected.aircraft_type)};
    body[count++] = {""};
    for (int i = 0; i < kAircraftNoticeLineCount; i++) body[count++] = {kAircraftNoticeLines[i]};
    draw_body(canvas, Heading::Interrupt, /*keys=*/true, body, count);
    draw_keys(canvas, {{Press::Once, "USE"}, "CLOSE"});
}

}  // namespace skyblip::go
