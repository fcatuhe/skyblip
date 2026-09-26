// Harness, not a test: the whole product on the host platform, stepped under a
// clock the case advances. Every product suite drives the device through this,
// so a case reads as what a pilot or a companion app does to the unit, and
// nothing below the services is stubbed out.
#ifndef SKYBLIP_TEST_SUPPORT_PRODUCT_RIG_H
#define SKYBLIP_TEST_SUPPORT_PRODUCT_RIG_H

#include <cstring>
#include <string>

#include "core/events/link.h"
#include "core/events/sensor.h"
#include "core/flight/atmosphere.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/input/controls.h"
#include "products/skyblip_go/product.h"

namespace skyblip {

using Go = go::Product<platform::host::Platform>;

struct Rig {
    platform::host::Platform platform;
    Go product{platform};

    explicit Rig(ports::Capabilities fitted = platform::host::Platform::kFullyFitted)
        : platform(fitted) {}

    Status setup() { return product.setup(); }

    go::Settings& settings() { return product.settings(); }

    void run(uint32_t from, uint32_t to, uint32_t step = 50) {
        for (uint32_t t = from; t <= to; t += step) {
            platform.clock().set_millis(t);
            product.step(t);
        }
    }

    // The shell's other loop: a refused boot settles the frame it owes the glass.
    void sleep_again(uint32_t step = 50) {
        for (uint32_t t = 0; t <= 20000; t += step) {
            platform.clock().set_millis(t);
            if (product.park_refusal(t)) return;
        }
    }

    void push_fix(int32_t alt_m, uint32_t updates) {
        gnss::GnssSolution f{};
        f.fix_valid = true;
        f.alt_mm = alt_m * 1000;
        f.vdop_e2 = 150;
        f.updates = updates;
        product.bus().gnss.push(f);
    }

    // A receiver that lost its vertical solution: no VDOP, and the last height it solved.
    void push_2d_fix(int32_t alt_m, uint32_t updates) {
        gnss::GnssSolution f{};
        f.fix_valid = true;
        f.fix_mode = gnss::kFixMode2D;
        f.alt_mm = alt_m * 1000;
        f.updates = updates;
        product.bus().gnss.push(f);
    }

    // 2026-08-02T00:00:00Z. A fix with no UTC cannot name a session, so the
    // helpers below carry one and advance it a second at a time.
    static constexpr uint32_t kUtcBase = 1785628800;

    // A solution as a receiver reports one in flight: moving, timed, solved in 3D, and
    // referenced to both datums. core/flight decides what it means.
    void push_timed_fix(int32_t speed_mm_s, int32_t alt_msl_m) {
        gnss::GnssSolution f{};
        f.fix_valid = true;
        f.utc_valid = true;
        f.utc = kUtcBase + utc_offset_s;
        f.lat_1e7 = 485000000 + static_cast<int32_t>(utc_offset_s) * 3000;
        f.lon_1e7 = 85000000;
        f.alt_msl_mm = alt_msl_m * 1000;
        f.alt_mm = f.alt_msl_mm + gnss::kDefaultGeoidSeparationMm;
        f.geoid_separation_measured = true;
        f.speed_mm_s = speed_mm_s;
        f.track_cdeg = 9000;
        f.sats = 10;
        f.hdop_e2 = 100;
        f.vdop_e2 = 150;
        f.updates = ++fix_updates;
        product.bus().gnss.push(f);
    }

    // One second of the world: a solution, then the passes that follow it.
    void second(uint32_t& t, int32_t speed_mm_s, int32_t alt_msl_m) {
        push_timed_fix(speed_mm_s, alt_msl_m);
        run(t, t + 950);
        t += 1000;
        utc_offset_s++;
    }

    // What the driver publishes once the receiver has gone quiet: not a fix.
    void blind_second(uint32_t& t) {
        gnss::GnssSolution f{};
        f.fix_valid = false;
        f.updates = ++fix_updates;
        product.bus().gnss.push(f);
        run(t, t + 950);
        t += 1000;
        utc_offset_s++;
    }

    void blind_seconds(uint32_t& t, uint32_t n) {
        for (uint32_t i = 0; i < n; i++) blind_second(t);
    }

    void seconds(uint32_t& t, uint32_t n, int32_t speed_mm_s, int32_t alt_msl_m) {
        for (uint32_t i = 0; i < n; i++) second(t, speed_mm_s, alt_msl_m);
    }

    // The same passes, counted instead of bounded: `t <= to` above cannot cross
    // the 49.7-day wrap of ports::Clock::millis(), because that is a comparison of
    // two instants (ports/clock.h). A case that wants the device stepped THROUGH the
    // wrap advances by an elapsed span instead. Steps t forward by span + step.
    void run_span(uint32_t& t, uint32_t span_ms, uint32_t step = 50) {
        for (uint32_t stepped = 0; stepped <= span_ms; stepped += step) {
            platform.clock().set_millis(t);
            product.step(t);
            t += step;
        }
    }

    // The same passes, driven from the 64-bit clock the way both platforms do it:
    // now_ms is the low 32 bits of the uptime micros() reports
    // (hardware/platform/{host,zephyr}/clock.h), so this is the only way to step a
    // case through the 49.7-day wrap of millis() while micros() keeps counting -
    // which is what the silicon does and what set_millis() cannot express.
    void run_span_from_us(uint64_t& us, uint32_t span_ms, uint32_t step_ms = 50) {
        for (uint32_t stepped = 0; stepped <= span_ms; stepped += step_ms) {
            platform.clock().set_micros(us);
            product.step(platform.clock().millis());
            us += static_cast<uint64_t>(step_ms) * 1000;
        }
    }

    // One second of the world, wherever the counter happens to be: run_span(950)
    // in 50 ms steps is the whole second.
    void second_across(uint32_t& t, int32_t speed_mm_s, int32_t alt_msl_m) {
        push_timed_fix(speed_mm_s, alt_msl_m);
        run_span(t, 950);
        utc_offset_s++;
    }

    void push_baro(int32_t alt_cm, uint32_t at_ms) {
        events::BaroSample sample{};
        sample.pressure_mpa = flight::alt_mm_to_pressure_mpa(alt_cm * 10);
        sample.at_ms = at_ms;
        product.bus().baro.push(sample);
    }

    // Held across steps, then released across steps: a level has to be stable
    // through the board's debounce window before it is an edge at all.
    void press(uint32_t& t) {
        platform.board_gpio().button_down = true;
        settle(t);
        platform.board_gpio().button_down = false;
        settle(t);
    }

    void settle(uint32_t& t) {
        for (int i = 0; i < 2; i++) {
            platform.clock().set_millis(t);
            product.step(t);
            t += 40;
        }
    }

    // The authorising gesture: two presses inside go::ConfirmGesture's window.
    void double_press(uint32_t& t) {
        press(t);
        press(t);
    }

    // The three gestures the device answers, as go::ScreenService names them.
    void tap(uint32_t& t) { touch_pad(t, 200); }

    void long_touch(uint32_t& t) { touch_pad(t, go::Controls::kLongTouchMs + 200); }

    // Reached the way a thumb reaches it: the pad along the walk, then the menu.
    void show(uint32_t& t, go::Page page) {
        for (int i = 0; i < go::kPageCount && product.screen().page() != go::menu_owner(page); i++)
            tap(t);
        if (go::walked(page)) return;
        press(t);
        const go::Menu menu = go::menu_for(go::menu_owner(page));
        for (int i = 0; i < menu.n && product.screen().editor().focus() != row_for(page); i++)
            tap(t);
        press(t);
        run(t, t + 200);
        t += 200;
    }

    static go::MenuRow row_for(go::Page page) {
        const go::Menu menu = go::menu_for(go::menu_owner(page));
        for (int i = 0; i < menu.n; i++)
            if (go::page_behind(menu.rows[i]) == page) return menu.rows[i];
        return go::MenuRow::kCount;
    }

    void touch_pad(uint32_t& t, uint32_t ms) {
        platform.board_gpio().pad_down = true;
        run(t, t + ms);
        t += ms;
        platform.board_gpio().pad_down = false;
        run(t, t + 100);
        t += 100;
    }

    bus::State& state() { return product.state(); }

    // A central connects, and a central goes away, through the platform's own
    // link model. Nothing here reaches into a service: the event travels
    // platform -> board -> bus -> config service, which is the path silicon uses.
    void raise_link(uint16_t session_id = 1) { platform.link().raise_link(session_id); }
    void drop_link() { platform.link().drop_link(); }
    void drop_link(uint16_t session_id) { platform.link().drop_link(session_id); }
    bool link_up() { return product.config().config().link_up(); }

    // The last thing the device said on one endpoint, the NMEA broadcast aside.
    std::string last_on(events::Endpoint endpoint) {
        const auto& sent = platform.link().sent;
        for (auto it = sent.rbegin(); it != sent.rend(); ++it)
            if (it->endpoint == endpoint) return it->bytes;
        return std::string();
    }

    // The companion app's side of the link, arriving where the board polls it.
    // An app that says something is an app that is connected.
    void send(const char* json) {
        if (!platform.link().up()) raise_link();
        send_from(platform.link().session_id(), json);
    }

    // Which app is talking, because config and the log answer one of them.
    void send_from(uint16_t session_id, const char* json) {
        events::RxFrame frame{};
        frame.session_id = session_id;
        frame.endpoint = events::Endpoint::Config;
        frame.len = static_cast<uint16_t>(std::strlen(json));
        std::memcpy(frame.data.data(), json, frame.len);
        platform.link().push_rx(frame);
    }

    // The same app on the log endpoint, which is a separate characteristic and
    // a separate queue.
    void send_log(const char* json) {
        if (!platform.link().up()) raise_link();
        send_log_from(platform.link().session_id(), json);
    }

    void send_log_from(uint16_t session_id, const char* json) {
        events::RxFrame frame{};
        frame.session_id = session_id;
        frame.endpoint = events::Endpoint::Log;
        frame.len = static_cast<uint16_t>(std::strlen(json));
        std::memcpy(frame.data.data(), json, frame.len);
        platform.link().push_rx(frame);
    }

    uint32_t utc_offset_s{0};
    uint32_t fix_updates{0};
};

// A board with no fitted barometer: the samples in these cases are pushed by
// hand, so the board must not also be pumping its own.
constexpr ports::Capabilities kBaroByHand =
    static_cast<ports::Capabilities>(static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
                                     ~static_cast<uint32_t>(ports::Capability::Baro));

}  // namespace skyblip

#endif
