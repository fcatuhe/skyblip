#include "products/skyblip_go/services/screen.h"
#include "protocols/adsb/registration.h"

namespace skyblip::go {

BravoView ScreenService::bravo_view() const {
    BravoView view;
    view.paired = paired(settings_);
    view.paired_tag = events::bluetooth_tag(settings_.bravo);
    view.link = bravo_.link();
    view.step = bravo_.step();
    view.refusal = bravo_.refusal();
    view.found_count = bravo_.found_count();
    for (int i = 0; i < view.found_count; i++)
        view.found_tags[i] = events::bluetooth_tag(bravo_.found(i));
    view.pairing_tag = events::bluetooth_tag(bravo_.pairing_with());
    return view;
}

void ScreenService::act_on_bravo(BravoAction action, uint32_t now_ms) {
    switch (action) {
        case BravoAction::None: return;
        case BravoAction::Redraw: dirty_ = true; return;
        case BravoAction::Leave: show_page(page_after(Page::Bravo)); return;
        case BravoAction::Pair:
            bravo_.pair(bravo_page_.focus(), now_ms);
            dirty_ = true;
            return;
        case BravoAction::Cancel:
        case BravoAction::Retry:
            bravo_.begin_pairing(now_ms);
            bravo_page_.open(now_ms);
            dirty_ = true;
            return;
        case BravoAction::Unpair:
            bravo_.unpair();
            show_page(page_after(Page::Bravo));
            return;
    }
}

void ScreenService::step_bravo(uint32_t now_ms) {
    if (on_bravo_page()) act_on_bravo(bravo_page_.idle(bravo_view(), now_ms), now_ms);
}

// INFO: fc 11nov26 once a boot, never over an alarm, and gone after the menu's idle hold
void ScreenService::sync_notice(uint32_t now_ms) {
    const Detected heard = detected();
    // INFO: fc 13nov26 an aircraft no longer heard on the ground, takeoff included, is no proposal
    const bool proposal_gone =
        notice_kind_ == Notice::NewAircraft && !same_aircraft(heard, noticed_aircraft_);
    if (notice_ && (alarm_takes_glass() || proposal_gone ||
                    now_ms - notice_since_ms_ >= MenuEditor::kIdleReturnHoldMs))
        close_notice();
    if (alarm_takes_glass()) return;
    // INFO: fc 11nov26 a lost pin outranks the own-ICAO notice, which no longer stands unpaired
    if (bravo_.take_forgotten_notice()) {
        notice_kind_ = Notice::BravoForgot;
    } else if (!notice_ && bravo_.own_icao_warning().take_notice(settings_.own_icao)) {
        notice_kind_ = Notice::OwnIcao;
    } else if (!notice_ && !aircraft_noticed_ && mode_ == Mode::Page && new_aircraft(heard)) {
        notice_kind_ = Notice::NewAircraft;
        noticed_aircraft_ = heard;
        aircraft_noticed_ = true;
    } else {
        return;
    }
    notice_ = true;
    notice_since_ms_ = now_ms;
    change_screen();
}

// INFO: fc 13nov26 on the ground only, so a wingman in the air is never proposed
Detected ScreenService::detected() const {
    Detected out;
    if (!paired(settings_)) return out;
    out.state = Detection::NotConnected;
    if (context_.state.bravo != comms::BravoState::Live) return out;
    out.state = Detection::Airborne;
    if (context_.state.flight.confirmed_state == flight::FlightState::Airborne) return out;
    out.state = Detection::NoFix;
    if (context_.state.flight.confirmed_state != flight::FlightState::Ground) return out;
    out.state = Detection::Listening;
    const std::optional<comms::OwnTransponderHeard> heard = bravo_.own_transponder();
    if (!heard) return out;
    out.state = Detection::Heard;
    out.address = heard->address;
    adsb::registration(out.callsign, sizeof(out.callsign), heard->callsign, heard->address);
    out.aircraft_type = heard->aircraft_category;
    return out;
}

// INFO: fc 13nov26 a press stores what the glass showed, while Bravo still hears exactly it
Detected ScreenService::detected_to_store(const Detected& shown) const {
    const Detected now = detected();
    if (shown.state == Detection::Heard && same_aircraft(shown, now)) return shown;
    if (shown.state != Detection::Heard && now.state != Detection::Heard) return now;
    Detected unseen = now;
    unseen.state = Detection::Listening;
    return unseen;
}

// INFO: fc 13nov26 once a boot, for an address the Go does not hold: a new aircraft or a first one
bool ScreenService::new_aircraft(const Detected& detected) const {
    return detected.state == Detection::Heard && detected.address != settings_.own_icao;
}

void ScreenService::use_noticed_aircraft() {
    if (!same_aircraft(detected(), noticed_aircraft_)) return;
    if (!adopt(noticed_aircraft_, settings_)) return;
    config_.note_settings_changed();
}

bool ScreenService::close_notice() {
    if (!notice_) return false;
    notice_ = false;
    change_screen();
    return true;
}

}  // namespace skyblip::go
