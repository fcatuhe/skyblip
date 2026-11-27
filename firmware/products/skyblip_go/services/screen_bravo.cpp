#include "core/units/units.h"
#include "products/skyblip_go/services/screen.h"
#include "protocols/adsb/registration.h"

namespace skyblip::go {

// INFO: fc 27nov26 the Go's fix, else its Bravo's in the same aircraft, for the glass alone
std::optional<model::Ownship> ScreenService::plotted_own() const {
    const model::Ownship& own = context_.state.own;
    if (own.fix_valid) return own;
    const comms::PeerHealth* health = bravo_.health();
    if (health == nullptr || !health->position) return std::nullopt;
    const comms::BravoPosition& position = *health->position;
    model::Ownship bravo = own;
    bravo.fix_valid = true;
    bravo.latitude_1e7 = position.latitude_1e7;
    bravo.longitude_1e7 = position.longitude_1e7;
    bravo.fix_at_ms = bravo_.health_heard_ms() - position.age_ms;
    bravo.altitude_hae_valid = position.altitude_m.has_value();
    bravo.altitude_hae_mm = position.altitude_m ? *position.altitude_m * 1000 : 0;
    bravo.speed_valid = position.speed_q.has_value();
    bravo.track_valid = position.speed_q.has_value();
    bravo.speed_mm_s =
        position.speed_q ? to_mm_s(QuarterMetresPerSecond(*position.speed_q)).value : 0;
    bravo.track_cdeg = to_centi_degrees(Cordic9(position.track_c9)).value;
    return bravo;
}

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
    if (const std::optional<uint16_t> wanted = bravo_.seeking()) {
        view.seeking = true;
        view.pairing_tag = *wanted;
    }
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
            bravo_.end_pairing();
            bravo_.begin_pairing(now_ms);
            bravo_page_.open(now_ms);
            dirty_ = true;
            return;
        case BravoAction::Unpair:
            bravo_.unpair();
            bravo_.begin_pairing(now_ms);
            bravo_page_.open(now_ms);
            dirty_ = true;
            return;
    }
}

void ScreenService::step_bravo(uint32_t now_ms) {
    if (bravo_.take_link_pairing_notice()) show_link_pairing(now_ms);
    if (on_bravo_page()) act_on_bravo(bravo_page_.idle(bravo_view(), now_ms), now_ms);
}

// INFO: fc 16nov26 a browser started the pairing, so opening the page starts and stops no scan
void ScreenService::show_link_pairing(uint32_t now_ms) {
    if (mode_ == Mode::Menu) leave_menu();
    (void)close_notice();
    page_ = Page::Bravo;
    bravo_page_.open(now_ms);
    change_screen();
}

uint32_t ScreenService::bravo_seconds_left(const BravoView& view, uint32_t now_ms) const {
    if (bravo_page_.confirming() || bravo_page_.unpairing())
        return bravo_page_.seconds_left(now_ms);
    if (!view.paired && view.step == BravoStep::Waiting)
        return seconds_left(bravo_.step_since_ms(), now_ms, BravoService::kAnswerCeilingMs);
    return 0;
}

// INFO: fc 11nov26 once a boot, never over an alarm, and gone after the menu's idle hold
void ScreenService::sync_notice(uint32_t now_ms) {
    const Detected heard = detected();
    // INFO: fc 13nov26 an aircraft no longer heard on the ground, takeoff included, is no proposal
    const bool proposal_gone =
        notice_kind_ == Notice::NewAircraft && !same_aircraft(heard, noticed_aircraft_);
    const bool answered = notice_kind_ == Notice::BravoAsks && !bravo_.asking_again();
    if (notice_ && (alarm_takes_glass() || proposal_gone || answered ||
                    now_ms - notice_since_ms_ >= notice_ceiling_ms()))
        close_notice();
    if (alarm_takes_glass()) return;
    if (bravo_.take_forgotten_notice()) {
        notice_kind_ = Notice::BravoForgot;
    } else if (bravo_.take_asking_notice()) {
        notice_kind_ = Notice::BravoAsks;
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
    if (!comms::dated(context_.state.bravo)) return out;
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
    return detected.state == Detection::Heard && detected.address != settings_.aircraft_address;
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
