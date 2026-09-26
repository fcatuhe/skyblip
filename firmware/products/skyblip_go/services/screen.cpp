#include "products/skyblip_go/services/screen.h"

#include <cstring>

#include "core/events/input.h"
#include "core/flight/atmosphere.h"
#include "core/flight/extrapolate.h"
#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/power/cutoff.h"
#include "core/protocol/nmea_out.h"
#include "core/timing/transmit.h"
#include "core/units/units.h"
#include "core/util/format.h"
#include "core/util/intmath.h"
#include "products/skyblip_go/pages/installing.h"
#include "ui/widgets/wordmark.h"

namespace skyblip::go {

namespace {
bool settled_for_a_double_press(uint32_t now_ms, uint32_t since_ms) {
    return now_ms - since_ms >= ConfirmGesture::kDoublePressMs;
}

// INFO: fc 17sep26 one edge a second, so a phase older than this is an edge that never came
constexpr uint32_t kPpsEdgeMissedMs = 1500;

PpsState pps_state(const timing::ClockState& clock) {
    if (!clock.pps_locked) return PpsState::None;
    return clock.ms_since_pps >= kPpsEdgeMissedMs ? PpsState::Holdover : PpsState::Lock;
}
}  // namespace

// INFO: cf 02aug26 a standing prompt takes the pad and the button both, so nothing pages or opens
void ScreenService::handle_input(uint32_t now_ms) {
    const comms::Pending pending = config_.pending();
    if (pending != prompt_) {
        prompt_ = pending;
        prompt_since_ms_ = now_ms;
        change_screen();
        confirm_.disarm();
        prompt_on_glass_ = false;
    }
    if (answering() && !confirm_.armed()) {
        // INFO: cf 02aug26 The two conditions that make a press an answer
        // rather than an accident: the question has reached the glass where it
        // can be read, and the thumb has stopped. A run of presses that began
        // before the prompt - pages being cycled, or a value being stepped on
        // the settings page - keeps the gesture disarmed until it ends, so
        // nothing already in flight can be spent on an authorisation. With no
        // panel fitted there is nothing to read and presence is all there is.
        const bool readable = prompt_on_glass_ ||
                              !ports::has(context_.roles.capabilities, ports::Capability::Display);
        const bool quiet = settled_for_a_double_press(now_ms, prompt_since_ms_) &&
                           (!pressed_once_ || settled_for_a_double_press(now_ms, last_press_ms_));
        if (readable && quiet) confirm_.arm(now_ms);
    }

    sync_editor(now_ms);
    sync_arming(now_ms);

    events::ContactEvent event{};
    while (context_.bus.input.pop(event)) {
        const Gesture gesture = controls_.read(event);
        record_contact(event, gesture, now_ms);
        obey(gesture, now_ms);
    }
    obey(controls_.tick(now_ms), now_ms);

    if (answering()) {
        resolve(confirm_.tick(now_ms));
        return;
    }
    if (arming_.tick(now_ms) != Answer::None) dirty_ = true;
    step_editor(now_ms);
}

void ScreenService::record_contact(const events::ContactEvent& event, Gesture gesture,
                                   uint32_t now_ms) {
    const int which = static_cast<int>(event.contact);
    const uint32_t held_ms = event.at_ms - contact_edge_ms_[which];
    contact_edge_ms_[which] = event.at_ms;
    if (!context_.diag.armed()) return;
    diag::Contact value{};
    value.at_ms = event.at_ms;
    value.held_ms = held_ms;
    value.contact = event.contact;
    value.gesture = static_cast<uint8_t>(gesture);
    value.down = event.down;
    context_.diag.record(value, context_.instant(now_ms));
}

void ScreenService::record_screen(uint32_t now_ms) {
    if (now_ms - recorded_ms_ < kRecordPeriodMs) return;
    recorded_ms_ = now_ms;
    if (!context_.diag.armed()) return;
    diag::Screen value{};
    value.since_ms = now_ms - screen_since_ms_;
    value.page = static_cast<uint8_t>(page_);
    value.mode = static_cast<uint8_t>(mode_);
    value.prompt = static_cast<uint8_t>(prompt_);
    value.alarm = context_.state.alarm_live;
    value.backlight = backlight_;
    value.powered = powered_;
    value.holding = thermal() == Thermal::Hold;
    context_.diag.record(value, context_.instant(now_ms));
}

// INFO: fc 20sep26 the price has to be on the glass before a press can be spent arming
void ScreenService::sync_arming(uint32_t now_ms) {
    if (answering() || !on_capture_page() || !capture_on_glass_ ||
        !context_.state.capture.available) {
        arming_.disarm();
        return;
    }
    if (!arming_.armed()) arming_.arm(now_ms);
}

void ScreenService::toggle_capture() {
    if (context_.diag.armed())
        context_.diag.disarm();
    else
        context_.diag.arm(capture_in_focus_);
}

bool ScreenService::picking_a_capture() const {
    return on_capture_page() && !context_.diag.armed() && context_.state.capture.available;
}

bool ScreenService::step_capture_focus() {
    if (!picking_a_capture()) return false;
    capture_in_focus_ = next_capture(capture_in_focus_);
    dirty_ = true;
    const bool walked_off_the_last_capture = capture_in_focus_ == kFirstCapture;
    return !walked_off_the_last_capture;
}

void ScreenService::obey(Gesture gesture, uint32_t now_ms) {
    switch (gesture) {
        case Gesture::Tap: tap(now_ms); return;
        case Gesture::LongTouch: long_touch(); return;
        case Gesture::Press: press(now_ms); return;
        case Gesture::None: return;
    }
}

void ScreenService::tap(uint32_t now_ms) {
    if (answering()) return;
    if (step_capture_focus()) return;
    page_forward(now_ms);
}

void ScreenService::long_touch() {
    if (answering()) return;
    if (alarm_stands()) {
        alarm_.dismiss();
        return;
    }
    show_radar();
}

void ScreenService::press(uint32_t now_ms) {
    last_press_ms_ = now_ms;
    pressed_once_ = true;
    if (answering()) {
        if (confirm_.armed()) resolve(confirm_.press(now_ms));
        return;
    }
    if (editor_.active()) {
        editor_.button(now_ms);
        return;
    }
    if (on_capture_page()) {
        if (arming_.press(now_ms) == Answer::Confirm) toggle_capture();
        dirty_ = true;
        return;
    }
    enter_menu(now_ms);
}

// INFO: cf 02aug26 the menu owns the button until a prompt takes it away unasked
void ScreenService::sync_editor(uint32_t now_ms) {
    const bool wanted = mode_ == Mode::Menu && !answering();
    if (wanted == editor_.active()) return;
    if (wanted) {
        editor_.enter(page_, now_ms);
        return;
    }
    editor_.leave();
}

void ScreenService::change_screen() {
    dirty_ = true;
    screen_since_ms_ = last_tick_ms_;
    if (change_ != Change::Wiped) change_ = Change::Asked;
}

void ScreenService::enter_menu(uint32_t now_ms) {
    if (menu_for(page_).n == 0) return;
    mode_ = Mode::Menu;
    sync_editor(now_ms);
    change_screen();
}

void ScreenService::page_forward(uint32_t now_ms) {
    if (mode_ != Mode::Menu) {
        next_page();
        return;
    }
    editor_.pad(now_ms);
}

void ScreenService::next_page() { show_page(next_fitted_page(page_)); }

// A page whose sensor is not on the board is not a stop on the walk: a plain
// T-Echo has no inertial sensor, so the g-meter is not one of its pictures.
Page ScreenService::next_fitted_page(Page from) const {
    Page page = page_after(from);
    for (int i = 0; i < kWalkedPages && !sensor_fitted(page); i++) page = page_after(page);
    return page;
}

bool ScreenService::sensor_fitted(Page page) const {
    if (page != Page::GMeter) return true;
    return ports::has(context_.roles.capabilities, ports::Capability::Inclinometer);
}

void ScreenService::show_radar() { show_page(Page::Radar); }

void ScreenService::show_page(Page page) {
    if (mode_ == Mode::Menu) leave_menu();
    page_ = page;
    change_screen();
}

void ScreenService::leave_menu() {
    mode_ = Mode::Page;
    const Page owner = editor_.page();
    editor_.leave();
    page_ = walked(owner) ? owner : Page::Radar;
    change_screen();
}

MenuValues ScreenService::menu_values() const {
    MenuValues values;
    values.settings = settings_;
    values.range_step = range_step_;
    return values;
}

void ScreenService::step_editor(uint32_t now_ms) {
    if (!editor_.active()) return;

    const MenuValues current = menu_values();
    MenuValues next;

    switch (editor_.tick(now_ms, current, next)) {
        case MenuAction::Changed:
            settings_ = next.settings;
            range_step_ = next.range_step;
            // INFO: cf 02aug26 One owner of the flash blob. The page changes the
            // struct the config service was already given a reference to and
            // says so with the same flag the companion link raises; the write
            // itself stays in go::ConfigLinkService::persist, so there is never
            // a second writer and never two versions of the blob.
            config_.note_settings_changed();
            dirty_ = true;
            break;
        case MenuAction::Moved: dirty_ = true; break;
        case MenuAction::Open: show_page(editor_.opening()); break;
        case MenuAction::Leave: leave_menu(); break;
        case MenuAction::None:
        default: break;
    }
}

void ScreenService::resolve(Answer answer) {
    if (answer == Answer::None) return;
    if (answer == Answer::Confirm)
        config_.confirm();
    else
        config_.cancel();
    prompt_ = comms::Pending::None;
    confirm_.disarm();
    prompt_on_glass_ = false;
    change_screen();
}

void ScreenService::tick(uint32_t now_ms) {
    last_tick_ms_ = now_ms;
    accrue_backlight(now_ms);
    handle_input(now_ms);
    context_.state.gnss.levels_wanted = showing_sky();

    if (context_.state.alarm_live != last_live_) {
        const bool escalated_into_glass =
            alarm_takes_glass() && context_.state.alarm_live > last_live_ && !showing_radar();
        last_live_ = context_.state.alarm_live;
        dirty_ = true;
        if (escalated_into_glass) show_radar();
    }

    if (mode_ == Mode::Menu && alarm_takes_glass()) leave_menu();
    record_screen(now_ms);

    if (!ports::has(context_.roles.capabilities, ports::Capability::Display)) return;
    settle_park(now_ms);
    if (!powered_) return;

    if (change_ != Change::Wiped && !refresh_allowed()) {
        context_.roles.display.ready(now_ms);
        return;
    }

    if (!dirty_ && now_ms - last_render_ms_ < kRenderPeriodMs) return;
    if (!context_.roles.display.ready(now_ms)) return;

    if (change_ == Change::Asked) {
        wipe_glass(now_ms);
        return;
    }
    if (presented_once_ && change_ != Change::Wiped && now_ms - last_present_ms_ < kPresentFloorMs)
        return;

    last_render_ms_ = now_ms;
    dirty_ = false;
    render(now_ms);

    const bool changed = !presented_once_ || change_ == Change::Wiped ||
                         std::memcmp(fb_.data(), presented_.data(), Glass::kBytes) != 0;
    if (!changed) return;

    context_.roles.display.present(fb_, ports::Refresh::Partial, now_ms);
    count_refresh(ports::Refresh::Partial);
    note_presented(now_ms);
    flash_alarm();
}

void ScreenService::flash_alarm() {
    if (!alarm_flashing()) return;
    alarm_flash_ = !alarm_flash_;
    dirty_ = true;
}

bool ScreenService::refresh_allowed() const {
    return thermal() == Thermal::Refresh &&
           power::may_refresh(context_.state.power.level, context_.state.power.supply_warned,
                              power::PanelRefresh::Routine);
}

// TODO: fc 12sep26 a cold glass is unmeasured, and no rule that returns is one full a frame (#62)
ScreenService::Thermal ScreenService::thermal() const {
    if (!context_.state.power.die_valid) return Thermal::Refresh;
    if (context_.state.power.die_dc > kHoldAboveDeciCelsius) return Thermal::Hold;
    return Thermal::Refresh;
}

// INFO: fc 09mar26 SoftRF changes page on partials alone: all black through the waveform, then it
void ScreenService::wipe_glass(uint32_t now_ms) {
    fb_.clear(/*white=*/false);
    context_.roles.display.paint_black(now_ms);
    count_refresh(ports::Refresh::Partial);
    note_presented(now_ms);
    prompt_on_glass_ = false;
    capture_on_glass_ = false;
    last_render_ms_ = now_ms;
    dirty_ = true;
    change_ = Change::Wiped;
}

void ScreenService::note_presented(uint32_t now_ms) {
    change_ = Change::None;
    flat_on_glass_ = false;
    std::memcpy(presented_.data(), fb_.data(), Glass::kBytes);
    presented_once_ = true;
    context_.state.panel_presented = true;
    prompt_on_glass_ = answering();
    capture_on_glass_ = on_capture_page();
    last_present_ms_ = now_ms;
}

void ScreenService::count_refresh(ports::Refresh mode) {
    bus::DutyState& duty = context_.state.duty;
    if (mode == ports::Refresh::Full)
        duty.panel_full_refreshes++;
    else
        duty.panel_partial_refreshes++;
}

void ScreenService::accrue_backlight(uint32_t now_ms) {
    lit_.observe(backlight_, now_ms);
    context_.state.duty.backlight_ms = lit_.ms();
}

void ScreenService::set_backlight(bool on) {
    backlight_ = on;
    context_.roles.display.set_backlight(on);
}

void ScreenService::set_power(bool on) {
    powered_ = on;
    if (on) {
        park_ = ParkStep::None;
        context_.roles.display.power_on();
        change_screen();
        return;
    }
    dirty_ = true;
    park_for_off();
}

// INFO: fc 01aug25 pushed before power-off: the glass wears it while off
void ScreenService::park(ParkFrame frame) {
    powered_ = false;
    park_frame_ = frame;
    park_ = may_present_park_frame() ? ParkStep::Frame : ParkStep::Sleep;
}

// INFO: fc 12sep26 both steps are commands, and a command sent over a live BUSY is lost
void ScreenService::settle_park(uint32_t now_ms) {
    if (park_ == ParkStep::None) return;
    if (!context_.roles.display.ready(now_ms)) return;
    if (park_ == ParkStep::Frame) {
        park_ = ParkStep::Sleep;
        draw_park_frame(park_frame_);
        context_.roles.display.present(fb_, ports::Refresh::Full, now_ms);
        count_refresh(ports::Refresh::Full);
        flat_on_glass_ = park_frame_ == ParkFrame::FlatCell;
        return;
    }
    park_ = ParkStep::None;
    context_.roles.display.power_off();
    accrue_backlight(now_ms);
    set_backlight(false);
}

void ScreenService::draw_park_frame(ParkFrame frame) {
    switch (frame) {
        case ParkFrame::Installing: draw_installing(fb_); return;
        // INFO: fc 12sep26 months of one image is the ghosting an e-paper never fully loses
        case ParkFrame::Blank: fb_.clear(/*white=*/true); return;
        case ParkFrame::FlatCell:
            fb_.clear(/*white=*/true);
            ui::draw_wordmark(fb_, kGlassW / 2, kGlassH / 2);
            draw_parked_flat_cell();
            return;
        case ParkFrame::Wordmark:
        default:
            fb_.clear(/*white=*/true);
            ui::draw_wordmark(fb_, kGlassW / 2, kGlassH / 2);
            return;
    }
}

void ScreenService::draw_parked_flat_cell() {
    const int wordmark_bottom = kGlassH / 2 + ui::wordmark_height() / 2;
    const int y = (wordmark_bottom + kGlassH) / 2 - kGlyphRows * kParkedSaidScale / 2;
    centred_text(y, "FLAT BATTERY", kParkedSaidScale);
}

void ScreenService::centred_text(int y, const char* text, int scale) {
    int n = 0;
    while (text[n] != 0) n++;
    const int width = n * kGlyphCols * scale;
    fb_.draw_text((kGlassW - width) / 2, y, text, true, scale);
}

bool ScreenService::may_present_park_frame() const {
    if (thermal() == Thermal::Hold) return false;
    return power::may_refresh(context_.state.power.level, context_.state.power.supply_warned,
                              power::PanelRefresh::Park);
}

// INFO: fc 07sep26 the glass wears this through the swap; a frozen prompt invites a reset
void ScreenService::park_for_install() { park(ParkFrame::Installing); }

void ScreenService::park_for_stow() { park(ParkFrame::Blank); }

void ScreenService::park_for_off() { park(ParkFrame::Wordmark); }

void ScreenService::park_for_flat_cell() { park(ParkFrame::FlatCell); }

void ScreenService::draw_prompt() {
    ConfirmSnapshot snapshot;
    snapshot.title = comms::pending_title(prompt_);
    snapshot.detail = comms::pending_detail(prompt_);
    snapshot.timeout_s = comms::kConfirmWindowMs / 1000;
    draw_confirm(fb_, snapshot);
}

void ScreenService::draw_menu_page() {
    if (editor_.editing()) {
        CallsignSnapshot field;
        field.text = editor_.text();
        field.cursor = editor_.cursor();
        field.clears = callsign_press_clears(field.text, field.cursor);
        draw_callsign(fb_, field);
        return;
    }
    MenuSnapshot snapshot;
    snapshot.page = editor_.page();
    snapshot.values = menu_values();
    snapshot.focus = editor_.focus();
    draw_menu(fb_, snapshot);
}

RawSnapshot ScreenService::raw_snapshot(uint32_t now_ms) const {
    const bus::State& state = context_.state;
    const model::OwnState& own = state.own;
    const timing::SlotTimingStats& stats = state.rf.timing_stats;

    RawSnapshot snap;
    snap.uptime_s = now_ms / 1000;

    snap.gnss.health = context_.roles.gnss.health();
    snap.gnss.stage = state.gnss.stage;
    snap.gnss.pps = pps_state(state.clock);
    snap.gnss.pps_age_ms = state.clock.ms_since_pps;
    snap.gnss.solutions = state.flight.gnss_solutions;
    snap.gnss.utc = own.utc;
    snap.gnss.nav_ms = state.gnss.solution_phase_ms;
    snap.gnss.nav_valid = state.gnss.solution_phase_valid;
    snap.gnss.hdop_e2 = own.hdop_e2;
    snap.gnss.vdop_e2 = own.vdop_e2;
    snap.gnss.resid_m = own.pred_resid_m;
    snap.gnss.resid_valid = own.pred_resid_valid;
    snap.gnss.sats = own.sats;
    snap.gnss.in_use = static_cast<uint8_t>(state.gnss.sky.in_use());
    snap.gnss.fix_mode = state.gnss.fix_mode;
    snap.gnss.fix_valid = own.fix_valid;
    snap.gnss.utc_valid = own.utc_valid;
    snap.gnss.settled = own.tx_settled;
    snap.gnss.levels_live = state.gnss.levels_live;

    snap.radio.rx_ok = state.air.rx_ok;
    snap.radio.rx_bad = state.air.rx_bad;
    snap.radio.rx_wait = state.air.rx_wait;
    snap.radio.rx_type = state.air.rx_type;
    snap.radio.rx_unframed = state.air.rx_unframed;
    snap.radio.rx_miskeyed = state.air.rx_miskeyed;
    snap.radio.rx_noise = state.air.rx_noise;
    snap.radio.rx_named = state.air.rx_named;
    snap.radio.uplink_frames = state.air.uplink_frames;
    snap.radio.uplink_bad = state.air.uplink_bad;
    snap.radio.uplink_targets = state.air.uplink_targets;
    snap.radio.tx_ok = state.air.tx_ok;
    snap.radio.tx_lost = state.air.tx_lost;
    snap.radio.tx_named = state.air.tx_named;
    snap.radio.missed = stats.missed();
    snap.radio.refused = stats.refused();
    snap.radio.duty_permille = state.rf.duty_permille;
    snap.radio.holdover = stats.holdover_events();
    snap.radio.dwell_worst_us = stats.dwell_worst_us();
    snap.radio.pps_worst_us = stats.pps_worst_us();
    snap.radio.tx_keyed_us = state.rf.last_tx_keyed_us;
    snap.radio.tx_span_us = state.rf.last_tx_span_us;
    snap.radio.tracked = static_cast<uint16_t>(state.traffic.count());
    snap.radio.alarm = traffic::to_number(state.alarm_level);
    snap.radio.noise_dbm = state.rf.noise_dbm;
    snap.radio.slot = state.rf.plan.state;
    snap.radio.freq_hz = state.rf.plan.freq_hz;
    snap.radio.tx_allowed = state.rf.plan.tx_allowed;
    return snap;
}

CaptureSnapshot ScreenService::capture_snapshot(uint32_t now_ms) const {
    CaptureSnapshot snap;
    snap.uptime_s = now_ms / 1000;
    snap.capture = context_.state.capture;
    snap.focus = capture_in_focus_;
    snap.running = context_.diag.profile();
    snap.focus_keeps_s = capture_.keeps_s(capture_in_focus_);
    snap.arming = arming_.pressed();
    return snap;
}

void ScreenService::render(uint32_t now_ms) {
    if (answering()) {
        draw_prompt();
        return;
    }
    if (mode_ == Mode::Menu) {
        draw_menu_page();
        return;
    }

    fb_.clear(/*white=*/true);

    const model::OwnState& own = context_.state.own;
    const go::Settings& settings = settings_;

    switch (page_) {
        case Page::Radar: {
            RadarSnapshot snap;
            snap.fix_valid = own.fix_valid;
            snap.stage = context_.state.gnss.stage;
            snap.units = settings.units;
            snap.range_step = range_step_;
            snap.track_cdeg = own.track_cdeg;
            snap.speed_mm_s = own.speed_mm_s;
            snap.turn_cdps = own.turn_cdps;
            snap.flight_seconds = context_.state.flight.seconds;
            snap.flight_time_valid = context_.state.flight.time_valid;
            snap.airborne = context_.state.flight.running;
            snap.taxiing = taxiing();
            snap.receiver_listening = receiver_listening();
            snap.alarm_flash = alarm_flash_;
            snap.formation_members = context_.state.formation.members;
            snap.battery_percent = context_.state.power.battery.percent;
            snap.battery_low = battery_low();
            int n = 0;
            if (own.fix_valid) {
                const model::OwnState own_now = flight::carried_to(own, now_ms);
                for (int i = 0; i < traffic::TrafficTable::kCapacity && n < kMaxRadarTargets; i++) {
                    const traffic::Target* t = context_.state.traffic.at(i);
                    if (!t || !t->used) continue;
                    const model::AircraftObs obs = flight::carried_to(t->obs, now_ms);
                    int32_t north = 0, east = 0, up = 0;
                    if (!protocol::relative_ned(own_now, obs, north, east, up)) continue;
                    targets_[n].north_m = north;
                    targets_[n].east_m = east;
                    targets_[n].up_m = up;
                    targets_[n].alarm_level = t->alarm_level;
                    targets_[n].alarm_dismissed = t->alarm_dismissed;
                    targets_[n].climb_e8 = obs.climb_e8;
                    targets_[n].climb_valid = obs.climb_valid;
                    targets_[n].speed_mm_s =
                        obs.speed_valid ? to_mm_s(QuarterMetresPerSec(obs.speed_q)).v : 0;
                    targets_[n].track_cdeg = to_centi_degrees(Cordic9(obs.track_c9)).v;
                    targets_[n].turn_cdps = static_cast<int16_t>(t->turn.dps * 100);
                    targets_[n].turn_valid = t->turn.valid;
                    targets_[n].in_formation = t->in_formation;
                    n++;
                }
            }
            snap.n_targets = n;
            snap.targets = targets_;
            draw_radar(fb_, snap);
            break;
        }
        case Page::SixPack: {
            SixPackSnapshot snap;
            snap.data_valid = own.fix_valid;
            snap.units = settings.units;
            snap.speed_kt = to_knots(MillimetresPerSec(own.speed_mm_s)).v;
            snap.alt_ft = to_feet(Millimetres(own.alt_mm)).v;
            snap.vs_fpm = climb_fpm();
            snap.vs_valid = climb_measured();
            snap.track_deg = to_degrees(CentiDegrees(own.track_cdeg)).v;
            snap.turn_cdps = own.turn_cdps;
            snap.battery_percent = context_.state.power.battery.percent;
            snap.battery_valid = context_.state.power.battery.valid;
            snap.inclinometer_fitted =
                ports::has(context_.roles.capabilities, ports::Capability::Inclinometer);
            snap.lateral_valid = context_.state.slip.valid;
            snap.lateral_mg = context_.state.slip.lateral_mg;
            draw_sixpack(fb_, snap);
            break;
        }
        case Page::Sats: {
            SatsSnapshot snap;
            snap.fix_valid = own.fix_valid;
            snap.levels_live = context_.state.gnss.levels_live;
            snap.stage = context_.state.gnss.stage;
            snap.stage_s = context_.state.gnss.stage_s;
            snap.sats = own.sats;
            snap.hdop_e2 = own.hdop_e2;
            snap.vdop_e2 = own.vdop_e2;
            snap.nav_ms = context_.state.gnss.solution_phase_ms;
            snap.nav_valid = context_.state.gnss.solution_phase_valid;
            snap.health = context_.roles.gnss.health();
            snap.sky = &context_.state.gnss.sky;
            draw_sats(fb_, snap);
            break;
        }
        case Page::Nearby: {
            NearbySnapshot snap;
            snap.own_addr = context_.roles.device_addr;
            snap.own_callsign = settings.callsign;
            snap.fix_valid = own.fix_valid;
            snap.units = settings.units;
            snap.n_heard = context_.state.traffic.count();
            snap.n_rows = traffic::rank_by_range(context_.state.traffic, context_.state.callsigns,
                                                 own, nearby_rows_, kNearbyRows);
            snap.rows = nearby_rows_;
            draw_nearby(fb_, snap);
            break;
        }
        case Page::SelfTest: draw_boot(fb_, self_test_); break;
        case Page::GMeter: {
            GMeterSnapshot snap;
            snap.fitted = ports::has(context_.roles.capabilities, ports::Capability::Inclinometer);
            snap.valid = context_.state.gload.valid;
            snap.now = context_.state.gload.now;
            snap.most = context_.state.gload.most;
            snap.least = context_.state.gload.least;
            draw_gmeter(fb_, snap);
            break;
        }
        case Page::RadioLog: {
            RadioLogSnapshot snap;
            snap.gnss.fix_valid = own.fix_valid;
            snap.gnss.sats = own.sats;
            snap.gnss.pps = pps_state(context_.state.clock);
            snap.gnss.pps_age_s = static_cast<uint16_t>(context_.state.clock.ms_since_pps / 1000);
            snap.rx_ok = context_.state.air.rx_ok;
            snap.tx_ok = context_.state.air.tx_ok;
            snap.noise = context_.state.air.rx_noise;
            snap.band_dbm = context_.state.rf.noise_dbm;
            snap.n_rows = context_.state.radio_log.count();
            snap.log = &context_.state.radio_log;
            draw_radio_log(fb_, snap);
            break;
        }
        case Page::Raw: {
            draw_raw(fb_, raw_snapshot(now_ms));
            break;
        }
        case Page::Capture: {
            draw_capture(fb_, capture_snapshot(now_ms));
            break;
        }
        case Page::Status:
        default: {
            StatusSnapshot snap;
            snap.device_addr = context_.roles.device_addr;
            snap.callsign = settings.callsign;
            snap.fix_valid = own.fix_valid;
            snap.utc_valid = own.utc_valid;
            snap.transmitting = timing::own_ship_transmits(own, context_.state.clock);
            snap.sats = own.sats;
            snap.stage = context_.state.gnss.stage;
            snap.stage_s = context_.state.gnss.stage_s;
            snap.fix_mode = context_.state.gnss.fix_mode;
            snap.lat_1e7 = own.lat_1e7;
            snap.lon_1e7 = own.lon_1e7;
            snap.alt_mm = own.alt_mm;
            snap.speed_mm_s = own.speed_mm_s;
            snap.track_cdeg = own.track_cdeg;
            snap.climb_mm_s = own.climb_mm_s;
            snap.utc = own.utc;
            snap.n_targets = context_.state.traffic.count();
            snap.imu_stage = context_.state.imu.stage;
            snap.imu_fault = context_.state.imu.fault;
            snap.imu_fifo_bytes = context_.state.imu.fifo_bytes;
            snap.imu_unparsed = context_.state.imu.unparsed;
            snap.imu_error = context_.state.imu.error;
            snap.imu_interrupt = context_.state.imu.interrupt;
            snap.imu_meta = context_.state.imu.meta;
            snap.imu_sensor_error = context_.state.imu.sensor_error;
            snap.imu_errored_sensor = context_.state.imu.errored_sensor;
            snap.slip_valid = context_.state.slip.valid;
            snap.slip_mg = context_.state.slip.lateral_mg;
            snap.baro_valid = context_.state.baro.active;
            snap.battery_valid = context_.state.power.battery.valid;
            snap.battery_mv = context_.state.power.battery.millivolts;
            snap.battery_percent = context_.state.power.battery.percent;
            snap.charging = context_.state.power.battery.charging;
            snap.charge = context_.state.power.charge;
            snap.battery_low = battery_low();
            snap.pressure_mpa = context_.state.baro.pressure_mpa;
            if (context_.state.baro.active) {
                const uint32_t pa = div_round<uint32_t>(context_.state.baro.pressure_mpa, 1000);
                snap.alt_std_m = div_round(flight::pressure_to_alt_cm(pa), 100);
            }
            draw_status(fb_, snap);
            break;
        }
    }
}

}  // namespace skyblip::go
