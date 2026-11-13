#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_AIRCRAFT_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_AIRCRAFT_H

#include <cstdint>

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

enum class Detection : uint8_t { Unpaired, NotConnected, Airborne, NoFix, Listening, Heard };

struct Detected {
    Detection state{Detection::Unpaired};
    uint32_t address{kOwnIcaoUnset};
    char callsign[kCallsignBytes]{};
    uint8_t aircraft_type{0};
};

// INFO: fc 13nov26 false when nothing heard would change, so a press on a match stores nothing
bool adopt(const Detected& detected, Settings& settings);
bool matches(const Detected& detected, const Settings& settings);
bool same_aircraft(const Detected& a, const Detected& b);

constexpr int kDetectedLines = 4;
constexpr const char* kAircraftNoticePadText = "PAD CLOSES";
constexpr const char* kAircraftNoticeButtonText = "BUTTON USES IT";
constexpr const char* kAircraftNoticeHelpText = "AS OWN ICAO, CALLSIGN AND TYPE";

void draw_detected(ui::Canvas& canvas, const Detected& detected, const Settings& settings,
                   bool focused);
void draw_aircraft_notice(ui::Canvas& canvas, const Detected& detected);

}  // namespace skyblip::go

#endif
