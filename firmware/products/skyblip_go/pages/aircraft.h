#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_AIRCRAFT_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_AIRCRAFT_H

#include <cstdint>

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

enum class Detection : uint8_t { Unpaired, NotConnected, Airborne, NoFix, Listening, Heard };

struct Detected {
    Detection state{Detection::Unpaired};
    uint32_t address{kAircraftAddressUnset};
    char callsign[kCallsignBytes]{};
    uint8_t aircraft_type{0};
};

// INFO: fc 13nov26 false when nothing heard would change, so a press on a match stores nothing
bool adopt(const Detected& detected, Settings& settings);
bool matches(const Detected& detected, const Settings& settings);
bool same_aircraft(const Detected& a, const Detected& b);

constexpr int kDetectedLines = 4;
// INFO: fc 16nov26 type, callsign, address, as the menu's rows and the Briefing page order them
constexpr int kDetectedTypeInset = 6;
constexpr int kDetectedNameInset = 24;
constexpr int kDetectedAddressInset = 50;
constexpr const char* kAircraftNoticeTitle = "BRAVO HEARS";

void draw_detected(ui::Canvas& canvas, const Detected& detected, const Settings& settings,
                   bool focused);
void draw_aircraft_notice(ui::Canvas& canvas, const Detected& detected, uint32_t seconds_left);

// INFO: fc 27nov26 what the Go hears aboard on 868 MHz: a FLARM ID it can store, or why not
enum class FlarmDetection : uint8_t { Airborne, NoFix, Listening, Heard, Icao, Other };

struct FlarmDetected {
    FlarmDetection state{FlarmDetection::Listening};
    uint32_t id{kFlarmIdUnset};
};

// INFO: fc 27nov26 false when the ID is already stored, so a press on a match stores nothing
bool adopt_flarm(const FlarmDetected& detected, Settings& settings);
bool flarm_matches(const FlarmDetected& detected, const Settings& settings);
bool same_flarm(const FlarmDetected& a, const FlarmDetected& b);

constexpr const char* kFlarmNoticeTitle = "GO HEARS";
constexpr const char* kFlarmIdLabel = "FLARM ID";

void draw_flarm_detected(ui::Canvas& canvas, const FlarmDetected& detected,
                         const Settings& settings, bool focused);
void draw_flarm_notice(ui::Canvas& canvas, const FlarmDetected& detected, uint32_t seconds_left);

}  // namespace skyblip::go

#endif
