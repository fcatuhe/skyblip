#include "products/skyblip_go/services/screen.h"

#include <cstring>

#include "core/events/input.h"
#include "core/power/cutoff.h"
#include "products/skyblip_go/pages/installing.h"
#include "products/skyblip_go/pages/recovery.h"
#include "ui/widgets/wordmark.h"

namespace skyblip::go {

namespace {
bool settled_for_a_double_press(uint32_t now_ms, uint32_t since_ms) {
    return now_ms - since_ms >= ConfirmGesture::kDoublePressMs;
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
    return values;
}

void ScreenService::step_editor(uint32_t now_ms) {
    if (!editor_.active()) return;

    const MenuValues current = menu_values();
    MenuValues next;

    switch (editor_.tick(now_ms, current, next)) {
        case MenuAction::Changed:
            settings_ = next.settings;
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
    cell_on_glass_ = power::CellOnGlass::None;
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
        cell_on_glass_ = park_frame_ == ParkFrame::FlatCell  ? power::CellOnGlass::Flat
                         : park_frame_ == ParkFrame::LowCell ? power::CellOnGlass::Low
                                                             : power::CellOnGlass::None;
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
        case ParkFrame::Recovery: draw_recovery(fb_, recovery_path_); return;
        // INFO: fc 12sep26 months of one image is the ghosting an e-paper never fully loses
        case ParkFrame::Blank: fb_.clear(/*white=*/true); return;
        case ParkFrame::FlatCell: draw_parked_cell("FLAT BATTERY"); return;
        case ParkFrame::LowCell: draw_parked_cell("CHARGE BATTERY"); return;
        case ParkFrame::Wordmark:
        default:
            fb_.clear(/*white=*/true);
            ui::draw_wordmark(fb_, kGlassW / 2, kGlassH / 2);
            return;
    }
}

void ScreenService::draw_parked_cell(const char* said) {
    fb_.clear(/*white=*/true);
    ui::draw_wordmark(fb_, kGlassW / 2, kGlassH / 2);
    const int wordmark_bottom = kGlassH / 2 + ui::wordmark_height() / 2;
    const int y = (wordmark_bottom + kGlassH) / 2 - kGlyphRows * kParkedSaidScale / 2;
    centred_text(y, said, kParkedSaidScale);
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

void ScreenService::park_for_recovery(ports::RecoveryPath path) {
    recovery_path_ = path;
    park(ParkFrame::Recovery);
}

void ScreenService::park_for_stow() { park(ParkFrame::Blank); }

void ScreenService::park_for_off() { park(ParkFrame::Wordmark); }

void ScreenService::park_for_flat_cell() { park(ParkFrame::FlatCell); }

void ScreenService::park_for_low_cell() { park(ParkFrame::LowCell); }

}  // namespace skyblip::go
