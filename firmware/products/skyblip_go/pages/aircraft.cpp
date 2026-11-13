#include "products/skyblip_go/pages/aircraft.h"

#include <cstring>

#include "core/util/format.h"
#include "products/skyblip_go/pages/menu.h"

namespace skyblip::go {

namespace {

constexpr int kHeaderY = 3;
constexpr int kHeaderRuleY = 21;
constexpr int kLabelInset = 6;
constexpr int kNameInset = 20;
constexpr int kTypeInset = 52;
constexpr int kStateInset = 30;
constexpr int kDetailInset = 56;
constexpr int kNoticeHeardY = 40;
constexpr int kNoticeNameY = 66;
constexpr int kNoticeDetailY = 100;
constexpr int kLabelBytes = 24;

const char* state_text(Detection state) {
    switch (state) {
        case Detection::Unpaired: return "NOT PAIRED";
        case Detection::NotConnected: return "NOT CONNECTED";
        case Detection::Airborne: return "IN FLIGHT";
        case Detection::NoFix: return "NO FIX";
        case Detection::Listening: return "LISTENING";
        case Detection::Heard: break;
    }
    return "";
}

const char* detail_text(Detection state) {
    switch (state) {
        case Detection::Unpaired: return "PAIR ONE TO DETECT THE AIRCRAFT";
        case Detection::Airborne:
        case Detection::NoFix: return "IT DETECTS ON THE GROUND";
        case Detection::Listening: return "TRANSPONDER TO ON OR ALT";
        case Detection::NotConnected:
        case Detection::Heard: break;
    }
    return "";
}

bool named_type(uint8_t code) { return code != 0 && code < kNamedAircraftTypes; }

// INFO: fc 13nov26 the address stands in for a transponder that sends no identification
const char* name_of(const Detected& detected, char* address) {
    address[format_hex(address, detected.address, kOwnIcaoCharacters)] = 0;
    return detected.callsign[0] != 0 ? detected.callsign : address;
}

void heard_label(char* out, const Detected& detected) {
    int written = format_string(out, "BRAVO HEARS ");
    written += format_hex(out + written, detected.address, kOwnIcaoCharacters);
    out[written] = 0;
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
        canvas.rectangle(2, top, kGlassWidth - 4, kDetectedLines * kMenuRowHeight - 1, true,
                         /*fill=*/true);
    const bool ink = !focused;
    if (detected.state != Detection::Heard) {
        canvas.draw_text(kMenuLeftX, top + kLabelInset, "BRAVO", ink, 1);
        canvas.draw_text(kMenuLeftX, top + kStateInset, state_text(detected.state), ink,
                         kMenuScale);
        canvas.draw_text(kMenuLeftX, top + kDetailInset, detail_text(detected.state), ink, 1);
        return;
    }
    char label[kLabelBytes];
    heard_label(label, detected);
    canvas.draw_text(kMenuLeftX, top + kLabelInset, label, ink, 1);
    char address[kOwnIcaoCharacters + 1];
    canvas.draw_text(kMenuLeftX, top + kNameInset, name_of(detected, address), ink, kCallsignScale);
    const char* type = matches(detected, settings) ? "MATCHES"
                       : named_type(detected.aircraft_type)
                           ? aircraft_type_name(detected.aircraft_type)
                           : "";
    canvas.draw_text(kMenuLeftX, top + kTypeInset, type, ink, kMenuScale);
}

void draw_aircraft_notice(ui::Canvas& canvas, const Detected& detected) {
    canvas.clear(true);
    canvas.draw_text(kMenuLeftX - 2, kHeaderY, "AIRCRAFT", true, kMenuScale);
    canvas.horizontal_line(kMenuLeftX - 2, kHeaderRuleY, kGlassWidth - 2 * (kMenuLeftX - 2), true);
    canvas.draw_text(centred_x("BRAVO HEARS", kMenuScale), kNoticeHeardY, "BRAVO HEARS", true,
                     kMenuScale);
    char address[kOwnIcaoCharacters + 1];
    const char* name = name_of(detected, address);
    canvas.draw_text(centred_x(name, kCallsignScale), kNoticeNameY, name, true, kCallsignScale);
    char detail[kLabelBytes];
    int written = format_hex(detail, detected.address, kOwnIcaoCharacters);
    if (named_type(detected.aircraft_type)) {
        written += format_string(detail + written, " ");
        written += format_string(detail + written, aircraft_type_name(detected.aircraft_type));
    }
    detail[written] = 0;
    canvas.draw_text(centred_x(detail, 1), kNoticeDetailY, detail, true, 1);
    canvas.draw_text(centred_x(kAircraftNoticePadText, kCallsignHintScale), kCallsignPadHintY,
                     kAircraftNoticePadText, true, kCallsignHintScale);
    canvas.draw_text(centred_x(kAircraftNoticeButtonText, kCallsignHintScale), kCallsignButtonHintY,
                     kAircraftNoticeButtonText, true, kCallsignHintScale);
    canvas.draw_text(centred_x(kAircraftNoticeHelpText, 1), kMenuHintY, kAircraftNoticeHelpText,
                     true, 1);
}

}  // namespace skyblip::go
