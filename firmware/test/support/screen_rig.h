// The screen service alone over the real SSD1681 driver and its model, state written by hand.
#ifndef SKYBLIP_TEST_SUPPORT_SCREEN_RIG_H
#define SKYBLIP_TEST_SUPPORT_SCREEN_RIG_H

#include "core/events/input.h"
#include "doctest/doctest.h"
#include "hardware/parts/ssd1681/model.h"
#include "hardware/parts/ssd1681/ssd1681.h"
#include "hardware/platform/host/clock.h"
#include "ports/null.h"
#include "products/skyblip_go/services/alarm.h"
#include "products/skyblip_go/services/capture.h"
#include "products/skyblip_go/services/screen.h"
#include "products/skyblip_go/settings_store.h"

using namespace skyblip;

namespace {

// The screen service alone: state is written by hand, so alarm level and
// traffic are exactly what the case says, with no alarm service re-deriving
// them.
struct Rig {
    models::Ssd1681 chip;
    parts::Ssd1681 epd{chip, chip, chip.dc, chip.rst, chip.busy};
    platform::host::Clock clock;
    ports::NullRoles null;
    ports::Roles roles{
        clock,          null.rf,        null.link,        epd,  // epd fills Display
        null.kv,        null.log_flash, null.annunciator, null.dfu, null.die_temperature,
        null.indicator, null.gnss};
    bus::Bus bus{};
    bus::State state{};
    diag::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::Settings settings{};
    go::SettingsStore store{settings, roles.device_addr};
    comms::ConfigService config{null.link, store};
    go::BootSnapshot self_test{};
    go::AlarmService alarm_service{context, settings};
    go::RecordPool pool{context};
    go::RecordStore capture_store{pool, store::SectorOwner::Diagnostics};
    go::RecordStore flights_store{pool, store::SectorOwner::Flights};
    go::CaptureService capture{context, capture_store, flights_store, settings, config};
    go::ScreenService screen{context, settings, config, alarm_service, capture, self_test};

    Rig() {
        chip.attach_clock(clock);
        roles.capabilities = ports::Capability::Display;
        // With a fix the radar page draws rings and the range label, so churn()
        // below produces real pixel changes.
        state.own.fix_valid = true;
        state.own.sats = 9;
        state.flight.time_valid = true;
        epd.begin();
    }

    // The pass a service loop makes: the world's clock, then the service on it.
    void tick(uint32_t now_ms) {
        clock.set_millis(now_ms);
        screen.tick(now_ms);
    }

    // One service tick per second, the render cadence.
    void run_seconds(uint32_t& t, int seconds) {
        for (int i = 0; i < seconds; i++) tick(t += 1000);
    }

    // Forces a visible change every second, orthogonal to alarm level and traffic count.
    void churn(uint32_t& t, int seconds) { churn_at(t, 1000, seconds); }

    // The same flip at the caller's cadence, for the policies measured in minutes.
    void churn_at(uint32_t& t, uint32_t step_ms, int times) {
        for (int i = 0; i < times; i++) {
            state.flight.seconds += 60;
            tick(t += step_ms);
        }
    }

    bool glass_all_black() const {
        return chip.framebuffer().count_black() == go::kGlassW * go::kGlassH;
    }

    // The pad held past the way home, a gesture read on the tick, not the release.
    void long_touch(uint32_t& t) {
        bus.input.push(events::ContactEvent{events::Contact::Pad, true, t});
        tick(t += 100);
        tick(t += go::Controls::kLongTouchMs);
        bus.input.push(events::ContactEvent{events::Contact::Pad, false, t});
        tick(t += 100);
    }

    // A press of the button, read on the release: the menu, then the row under the focus.
    void press(uint32_t& t) {
        bus.input.push(events::ContactEvent{events::Contact::Button, true, t});
        tick(t += 100);
        bus.input.push(events::ContactEvent{events::Contact::Button, false, t});
        tick(t += 100);
    }

    // The pad tapped: the next page along the walk, or the next row down a menu.
    void tap(uint32_t& t) {
        bus.input.push(events::ContactEvent{events::Contact::Pad, true, t});
        tick(t += 100);
        bus.input.push(events::ContactEvent{events::Contact::Pad, false, t});
        tick(t += 100);
    }

    // Reached the way a thumb reaches it: the pad along the walk, then the menu row that opens it.
    void show(uint32_t& t, go::Page page) {
        for (int i = 0; i < go::kPageCount && screen.page() != go::menu_owner(page); i++) tap(t);
        if (go::walked(page)) return;
        press(t);
        const go::Menu menu = go::menu_for(go::menu_owner(page));
        for (int i = 0; i < menu.n && go::page_behind(screen.editor().focus()) != page; i++) tap(t);
        press(t);
    }

    void alarm(traffic::Level level) {
        state.alarm_level = level;
        state.alarm_live = level;
    }

    // The long touch as the alarm service publishes it: the sky stands, nothing is live.
    void dismiss() {
        state.alarm_live = traffic::Level::None;
        for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
            traffic::Target* t = state.traffic.at(i);
            if (t && t->used) t->alarm_dismissed = true;
        }
    }

    // A contact at a bearing, graded by hand, for the page to point at.
    void threat(traffic::Level level, int32_t north_m, int32_t east_m, uint32_t now_ms) {
        model::AircraftObs obs{};
        obs.addr = kThreatAddr;
        obs.addr_table = kThreatTable;
        obs.position_valid = true;
        obs.lat_1e7 = state.own.lat_1e7 + static_cast<int32_t>(int64_t(north_m) * 1000000 / 11132);
        obs.lon_1e7 = state.own.lon_1e7 + static_cast<int32_t>(int64_t(east_m) * 1000000 / 11132);
        obs.alt_m = to_metres(Millimetres(state.own.alt_mm)).v;
        obs.received.at_s = now_ms / 1000;
        obs.at_ms = now_ms;
        obs.source = model::Source::AdslDirect;
        state.traffic.update(obs, now_ms / 1000);
        const int at = state.traffic.find(kThreatTable, kThreatAddr);
        REQUIRE(at >= 0);
        state.traffic.at(at)->alarm_level = level;
        alarm(level);
    }

    static constexpr uint32_t kThreatAddr = 0x424242;
    static constexpr uint8_t kThreatTable = 6;

    void die_temperature(int16_t decicelsius) {
        state.power.die_dc = decicelsius;
        state.power.die_valid = true;
    }
};

}  // namespace

#endif
