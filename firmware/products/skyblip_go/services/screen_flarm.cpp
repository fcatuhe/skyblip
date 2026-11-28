#include "products/skyblip_go/services/screen.h"

namespace skyblip::go {

// INFO: fc 27nov26 on the ground only, so a wingman's FLARM in the air is never proposed
FlarmDetected ScreenService::flarm_detected() const {
    FlarmDetected out;
    out.state = FlarmDetection::Airborne;
    if (context_.state.flight.confirmed_state == flight::FlightState::Airborne) return out;
    out.state = FlarmDetection::NoFix;
    if (context_.state.flight.confirmed_state != flight::FlightState::Ground) return out;
    out.state = FlarmDetection::Listening;
    const std::optional<traffic::AboardHeard>& aboard = context_.state.aboard;
    if (!aboard) return out;
    out.id = aboard->address;
    switch (aboard->address_mapping_table) {
        case adsl::kAddressTableFlarm: out.state = FlarmDetection::Heard; break;
        case adsl::kAddressTableIcao: out.state = FlarmDetection::Icao; break;
        default: out.state = FlarmDetection::Other; break;
    }
    return out;
}

// INFO: fc 27nov26 a press stores what the glass showed, while the Go still hears exactly it
FlarmDetected ScreenService::flarm_to_store(const FlarmDetected& shown) const {
    const FlarmDetected now = flarm_detected();
    if (shown.state == FlarmDetection::Heard && same_flarm(shown, now)) return shown;
    if (shown.state != FlarmDetection::Heard && now.state != FlarmDetection::Heard) return now;
    FlarmDetected unseen = now;
    unseen.state = FlarmDetection::Listening;
    return unseen;
}

// INFO: fc 27nov26 once a boot, for a FLARM ID the Go does not hold: a new aircraft or a first one
bool ScreenService::new_flarm(const FlarmDetected& detected) const {
    return detected.state == FlarmDetection::Heard && detected.id != settings_.flarm_id;
}

void ScreenService::use_noticed_flarm() {
    if (!same_flarm(flarm_detected(), noticed_flarm_)) return;
    if (!adopt_flarm(noticed_flarm_, settings_)) return;
    config_.note_settings_changed();
}

}  // namespace skyblip::go
