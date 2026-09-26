#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_SCREEN_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_SCREEN_H

#include "core/comms/config.h"
#include "core/diag/payload.h"
#include "core/flight/state.h"
#include "core/power/duty.h"
#include "core/units/units.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/input/controls.h"
#include "products/skyblip_go/input/gesture.h"
#include "products/skyblip_go/pages/boot.h"
#include "products/skyblip_go/pages/capture.h"
#include "products/skyblip_go/pages/confirm.h"
#include "products/skyblip_go/pages/gmeter.h"
#include "products/skyblip_go/pages/menu.h"
#include "products/skyblip_go/pages/nearby.h"
#include "products/skyblip_go/pages/page.h"
#include "products/skyblip_go/pages/radar.h"
#include "products/skyblip_go/pages/radio_log.h"
#include "products/skyblip_go/pages/raw.h"
#include "products/skyblip_go/pages/sats.h"
#include "products/skyblip_go/pages/sixpack.h"
#include "products/skyblip_go/pages/status.h"
#include "products/skyblip_go/services/alarm.h"
#include "products/skyblip_go/services/capture.h"
#include "runtime/service.h"

namespace skyblip::go {

enum class Mode : uint8_t { Page = 0, Menu = 1 };

class ScreenService : public runtime::Service {
   public:
    static constexpr uint32_t kRenderPeriodMs = 1000;
    static constexpr uint32_t kPresentFloorMs = 1000;

    // INFO: fc 20sep26 the render cadence: a capture says what was on the glass, not what was drawn
    static constexpr uint32_t kRecordPeriodMs = kRenderPeriodMs;

    // INFO: fc 06sep26 Good Display rates the glass 0..50 C, read on a die above ambient
    static constexpr int16_t kHoldAboveDeciCelsius = 500;

    // INFO: cf 02aug26 at this level a menu in front of converging traffic is a bug, so it goes
    static constexpr traffic::Level kAlarmTakesGlass = traffic::Level::Advisory;

    ScreenService(runtime::Context& context, Settings& settings, comms::ConfigService& config,
                  AlarmService& alarm, const CaptureService& capture, const BootSnapshot& self_test)
        : runtime::Service(context),
          settings_(settings),
          config_(config),
          alarm_(alarm),
          capture_(capture),
          self_test_(self_test) {}

    void tick(uint32_t now_ms) override;

    void next_page();
    Page next_fitted_page(Page from) const;
    bool sensor_fitted(Page page) const;
    void set_backlight(bool on);
    void set_power(bool on);
    void settle_park(uint32_t now_ms);
    void park_for_install();
    void park_for_stow();
    void park_for_off();
    void park_for_flat_cell();
    void set_range_step(int step) {
        range_step_ = clamped_range_step(step);
        dirty_ = true;
    }

    enum class Thermal : uint8_t { Refresh, Hold };
    Thermal thermal() const;

    Page page() const { return page_; }
    Mode mode() const { return mode_; }
    comms::Pending prompt() const { return prompt_; }
    const MenuEditor& editor() const { return editor_; }
    int range_step() const { return range_step_; }
    bool backlight() const { return backlight_; }
    bool powered() const { return powered_; }
    bool parking() const { return park_ != ParkStep::None; }
    bool flat_on_glass() const { return flat_on_glass_; }
    const Glass& framebuffer() const { return fb_; }
    void mark_dirty() { dirty_ = true; }

   private:
    void render(uint32_t now_ms);
    RawSnapshot raw_snapshot(uint32_t now_ms) const;
    CaptureSnapshot capture_snapshot(uint32_t now_ms) const;
    bool on_capture_page() const { return mode_ == Mode::Page && page_ == Page::Capture; }
    bool picking_a_capture() const;
    bool step_capture_focus();
    void sync_arming(uint32_t now_ms);
    void toggle_capture();
    void change_screen();
    void draw_prompt();
    void draw_menu_page();
    void enter_menu(uint32_t now_ms);
    void leave_menu();
    void page_forward(uint32_t now_ms);
    void show_radar();
    void show_page(Page page);
    void handle_input(uint32_t now_ms);
    void record_contact(const events::ContactEvent& event, Gesture gesture, uint32_t now_ms);
    void record_screen(uint32_t now_ms);
    void obey(Gesture gesture, uint32_t now_ms);
    void tap(uint32_t now_ms);
    void long_touch();
    void press(uint32_t now_ms);
    void sync_editor(uint32_t now_ms);
    void step_editor(uint32_t now_ms);
    MenuValues menu_values() const;
    void resolve(Answer answer);
    enum class Change : uint8_t { None, Asked, Wiped };
    bool refresh_allowed() const;
    void wipe_glass(uint32_t now_ms);
    bool may_present_park_frame() const;
    enum class ParkFrame : uint8_t { Wordmark, Installing, Blank, FlatCell };
    enum class ParkStep : uint8_t { None, Frame, Sleep };
    void park(ParkFrame frame);
    void draw_park_frame(ParkFrame frame);
    void draw_parked_flat_cell();
    void centred_text(int y, const char* text, int scale);
    static constexpr int kParkedSaidScale = 2;
    static constexpr int kGlyphCols = 6;
    static constexpr int kGlyphRows = 7;
    void note_presented(uint32_t now_ms);
    void count_refresh(ports::Refresh mode);
    void accrue_backlight(uint32_t now_ms);

    int32_t climb_fpm() const {
        return to_feet_per_minute(MillimetresPerSec(context_.state.own.climb_mm_s)).v;
    }

    bool climb_measured() const {
        return context_.state.own.climb_valid &&
               (context_.state.own.fix_valid || context_.state.baro.active);
    }

    bool showing_sky() const {
        return powered_ && mode_ == Mode::Page && page_ == Page::Sats &&
               ports::has(context_.roles.capabilities, ports::Capability::Display);
    }

    bool showing_radar() const { return mode_ == Mode::Page && page_ == Page::Radar; }

    bool answering() const { return prompt_ != comms::Pending::None; }

    bool alarm_takes_glass() const {
        return context_.state.alarm_live >= kAlarmTakesGlass && !diagnostics_on_glass();
    }

    bool diagnostics_on_glass() const {
        return mode_ == Mode::Menu ? diagnostics_menu(editor_.page()) : diagnostic(page_);
    }

    bool alarm_stands() const { return context_.state.alarm_live != traffic::Level::None; }

    bool alarm_flashing() const { return alarm_stands(); }

    void flash_alarm();

    bool receiver_listening() const {
        return ports::has(context_.roles.capabilities, ports::Capability::Rf) &&
               context_.state.clock.pps_locked;
    }

    bool taxiing() const {
        return context_.state.own.fix_valid && !context_.state.flight.running &&
               context_.state.flight.rolling;
    }

    // INFO: fc 20sep26 core/power debounced it, dropped a charged cell and a floating sense
    bool battery_low() const {
        return context_.state.power.level == power::PowerLevel::Low ||
               context_.state.power.level == power::PowerLevel::Cutoff;
    }

    Settings& settings_;
    comms::ConfigService& config_;
    AlarmService& alarm_;
    const CaptureService& capture_;
    const BootSnapshot& self_test_;
    diag::Profile capture_in_focus_{kFirstCapture};
    comms::Pending prompt_{comms::Pending::None};
    Controls controls_{};
    ConfirmGesture confirm_{};
    ConfirmGesture arming_{};
    MenuEditor editor_{};

    // INFO: cf 02aug26 a prompt is answered once read and once the thumb has stopped, never sooner
    uint32_t last_press_ms_{0};
    uint32_t prompt_since_ms_{0};
    bool pressed_once_{false};
    bool prompt_on_glass_{false};
    bool capture_on_glass_{false};

    Glass fb_{};
    Glass presented_{};
    RadarTarget targets_[kMaxRadarTargets]{};
    traffic::RangeRow nearby_rows_[kNearbyRows]{};
    Page page_{Page::Radar};
    Mode mode_{Mode::Page};
    int range_step_{kDefaultRangeStep};
    uint32_t last_tick_ms_{0};
    uint32_t screen_since_ms_{0};
    uint32_t recorded_ms_{0};
    uint32_t contact_edge_ms_[2]{};
    uint32_t last_render_ms_{0};
    uint32_t last_present_ms_{0};
    traffic::Level last_live_{traffic::Level::None};
    bool alarm_flash_{false};
    bool dirty_{true};
    Change change_{Change::Asked};
    bool presented_once_{false};
    ParkStep park_{ParkStep::None};
    ParkFrame park_frame_{ParkFrame::Wordmark};
    bool flat_on_glass_{false};
    power::OnTime lit_{};
    bool backlight_{false};
    bool powered_{true};
};

}  // namespace skyblip::go

#endif
