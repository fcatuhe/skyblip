// Harness, not a test: the whole product on the host platform, stepped under a
// clock the case advances. Every product suite drives the device through this,
// so a case reads as what a pilot or a companion app does to the unit, and
// nothing below the services is stubbed out.
#ifndef SKYBLIP_TEST_SUPPORT_PRODUCT_RIG_H
#define SKYBLIP_TEST_SUPPORT_PRODUCT_RIG_H

#include <cstring>
#include <functional>
#include <string>

#include "core/events/link.h"
#include "core/events/sensor.h"
#include "core/flight/atmosphere.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/input/controls.h"
#include "products/skyblip_go/product.h"
#include "test/support/config_frame.h"
#include "test/support/timed_fix.h"

namespace skyblip {

using Go = go::Product<platform::host::Platform>;

// INFO: fc 18oct26 the barometer is required, so these cases fit one and silence it after bring-up
struct BarometerByHand {};
constexpr BarometerByHand kBarometerByHand{};

struct Rig {
    platform::host::Platform platform;
    Go product{platform};

    explicit Rig(BarometerByHand) : Rig() { barometer_by_hand_ = true; }

    explicit Rig(ports::Capabilities fitted = platform::host::Platform::kFullyFitted,
                 std::optional<uint16_t> serial = platform::host::Identity::kSerial)
        : platform(fitted, serial) {}

    go::Glass self_test_page() const {
        go::Glass page{};
        go::draw_self_test(page, product.self_test());
        return page;
    }

    Status setup() {
        const Status status = product.setup();
        if (barometer_by_hand_) platform.barometer().fitted = false;
        return status;
    }

    bool barometer_by_hand_{false};

    go::Settings& settings() { return product.settings(); }

    // One pass of the loop, with the receiver talking on its UART once the case brought it up.
    void pass(uint32_t at_ms) {
        if (receiver_live_) platform.chips().gnss.tick(at_ms);
        product.run_pass(at_ms);
    }

    // The L76K model brought up to 2 Hz at 115200 by the driver, as on the device, before any
    // fix. From then on the timed fixes a case flies are the model's own, reported through the
    // driver as on the device, so the Go transmits.
    [[nodiscard]] bool bring_receiver_up(uint32_t& clock_ms) {
        models::L76k& chip = platform.chips().gnss;
        platform.retune_gnss_port(true);
        chip.fix_valid = false;
        receiver_live_ = true;
        for (int step = 0; step < 400 && !product.board().gnss().holds_rate(); step++) {
            platform.clock().set_milliseconds(clock_ms);
            pass(clock_ms);
            if (world) world(clock_ms);
            clock_ms += 50;
        }
        clock_ms = (clock_ms / 1000 + 1) * 1000;
        return product.board().gnss().holds_rate();
    }

    bool receiver_live_{false};

    void run(uint32_t from_ms, uint32_t to_ms, uint32_t step_ms = 50) {
        for (uint32_t at_ms = from_ms; at_ms <= to_ms; at_ms += step_ms) {
            platform.clock().set_milliseconds(at_ms);
            pass(at_ms);
            if (world) world(at_ms);
        }
    }

    // The rig's own clock, which every rig steps the same way: passes from now_ms to now_ms +
    // span_ms, both included, then the clock moves on by the span.
    uint32_t now_ms{0};

    void run_for(uint32_t span_ms, uint32_t step_ms = 50) {
        run(now_ms, now_ms + span_ms, step_ms);
        now_ms += span_ms;
    }

    // INFO: fc 11nov26 what else lives through each pass, a paired Bravo and the air between
    std::function<void(uint32_t)> world{};

    // The shell's other loop: a refused boot settles the frame it owes the glass.
    void sleep_again(uint32_t step_ms = 50) {
        for (uint32_t at_ms = 0; at_ms <= 20000; at_ms += step_ms) {
            platform.clock().set_milliseconds(at_ms);
            if (product.park_refusal(at_ms)) return;
        }
    }

    void push_fix(int32_t altitude_m, uint32_t updates) {
        gnss::GnssSolution solution{};
        solution.fix_valid = true;
        solution.speed_valid = true;
        solution.track_valid = true;
        solution.altitude_hae_mm = altitude_m * 1000;
        solution.vdop_e2 = 150;
        solution.sentences = updates;
        product.bus().gnss.push(solution);
    }

    // A receiver that lost its vertical solution: no VDOP, and the last height it solved.
    void push_2d_fix(int32_t altitude_m, uint32_t updates) {
        gnss::GnssSolution solution{};
        solution.fix_valid = true;
        solution.speed_valid = true;
        solution.track_valid = true;
        solution.fix_mode = gnss::kFixMode2D;
        solution.altitude_hae_mm = altitude_m * 1000;
        solution.sentences = updates;
        product.bus().gnss.push(solution);
    }

    // A solution as a receiver reports one in flight, moving east a second at a time.
    void push_timed_fix(int32_t speed_mm_s, int32_t altitude_msl_m) {
        if (receiver_live_) {
            fly_receiver(speed_mm_s, altitude_msl_m);
            return;
        }
        product.bus().gnss.push(timed_fix({.utc_offset_s = utc_offset_s,
                                           .sentences = ++fix_updates,
                                           .latitude_1e7 = flown_latitude_1e7(),
                                           .altitude_msl_mm = altitude_msl_m * 1000,
                                           .speed_mm_s = speed_mm_s,
                                           .track_cdeg = 9000}));
    }

    int32_t flown_latitude_1e7() const {
        return kFixLatitude1e7 + static_cast<int32_t>(utc_offset_s) * 3000;
    }

    // INFO: fc 19nov26 push_timed_fix's solution, set as the model's truth for its next solve
    void fly_receiver(int32_t speed_mm_s, int32_t altitude_msl_m) {
        models::L76k& chip = platform.chips().gnss;
        chip.fix_valid = true;
        chip.latitude_1e7 = flown_latitude_1e7();
        chip.longitude_1e7 = kFixLongitude1e7;
        chip.geoid_separation_m = gnss::kDefaultGeoidSeparationMm / 1000;
        chip.altitude_m = altitude_msl_m + chip.geoid_separation_m;
        chip.speed_mm_s = speed_mm_s;
        chip.track_deg = 90;
        chip.date = "020826";
        chip.utc_seconds_of_day = (kBaseUtcS + utc_offset_s) % 86400;
        ++fix_updates;
    }

    // One second of the world: a solution, then the passes that follow it.
    void second(uint32_t& clock_ms, int32_t speed_mm_s, int32_t altitude_msl_m) {
        push_timed_fix(speed_mm_s, altitude_msl_m);
        run(clock_ms, clock_ms + 950);
        clock_ms += 1000;
        utc_offset_s++;
    }

    // What the driver publishes once the receiver has gone quiet: not a fix.
    void blind_second(uint32_t& clock_ms) {
        gnss::GnssSolution solution{};
        solution.fix_valid = false;
        solution.sentences = ++fix_updates;
        product.bus().gnss.push(solution);
        run(clock_ms, clock_ms + 950);
        clock_ms += 1000;
        utc_offset_s++;
    }

    void blind_seconds(uint32_t& clock_ms, uint32_t seconds) {
        for (uint32_t i = 0; i < seconds; i++) blind_second(clock_ms);
    }

    void seconds(uint32_t& clock_ms, uint32_t seconds, int32_t speed_mm_s, int32_t altitude_msl_m) {
        for (uint32_t i = 0; i < seconds; i++) second(clock_ms, speed_mm_s, altitude_msl_m);
    }

    // The same passes, counted instead of bounded: `at_ms <= to_ms` above cannot cross
    // the 49.7-day wrap of ports::Clock::milliseconds(), because that is a comparison of
    // two instants (ports/clock.h). A case that wants the device stepped THROUGH the
    // wrap advances by an elapsed span instead. Steps clock_ms forward by span + step_ms.
    void run_span(uint32_t& clock_ms, uint32_t span_ms, uint32_t step_ms = 50) {
        for (uint32_t stepped = 0; stepped <= span_ms; stepped += step_ms) {
            platform.clock().set_milliseconds(clock_ms);
            pass(clock_ms);
            clock_ms += step_ms;
        }
    }

    // The same passes, driven from the 64-bit clock the way both platforms do it:
    // now_ms is the low 32 bits of the uptime microseconds() reports
    // (hardware/platform/{host,zephyr}/clock.h), so this is the only way to step a
    // case through the 49.7-day wrap of milliseconds() while microseconds() keeps counting -
    // which is what the silicon does and what set_milliseconds() cannot express.
    void run_span_from_us(uint64_t& now_us, uint32_t span_ms, uint32_t step_ms = 50) {
        for (uint32_t stepped = 0; stepped <= span_ms; stepped += step_ms) {
            platform.clock().set_microseconds(now_us);
            pass(platform.clock().milliseconds());
            now_us += static_cast<uint64_t>(step_ms) * 1000;
        }
    }

    // One second of the world, wherever the counter happens to be: run_span(950)
    // in 50 ms steps is the whole second.
    void second_across(uint32_t& clock_ms, int32_t speed_mm_s, int32_t altitude_msl_m) {
        push_timed_fix(speed_mm_s, altitude_msl_m);
        run_span(clock_ms, 950);
        utc_offset_s++;
    }

    void push_barometer(int32_t altitude_cm, uint32_t at_ms) {
        events::BarometerSample sample{};
        sample.pressure_mpa = flight::altitude_mm_to_pressure_mpa(altitude_cm * 10);
        sample.at_ms = at_ms;
        product.bus().barometer.push(sample);
    }

    // Held across steps, then released across steps: a level has to be stable
    // through the board's debounce window before it is an edge at all.
    void press(uint32_t& clock_ms) {
        platform.board_gpio().button_down = true;
        settle(clock_ms);
        platform.board_gpio().button_down = false;
        settle(clock_ms);
    }

    void settle(uint32_t& clock_ms) {
        for (int i = 0; i < 2; i++) {
            platform.clock().set_milliseconds(clock_ms);
            pass(clock_ms);
            if (world) world(clock_ms);
            clock_ms += 40;
        }
    }

    // The three gestures the device answers, as go::ScreenService names them.
    void tap(uint32_t& clock_ms) { touch_pad(clock_ms, 200); }

    void long_touch(uint32_t& clock_ms) {
        touch_pad(clock_ms, go::Controls::kLongTouchHoldMs + 200);
    }

    // Reached the way a thumb reaches it: the pad along the walk, then the menu.
    void show(uint32_t& clock_ms, go::Page page) {
        for (int i = 0; i < go::kPageCount && product.screen().page() != go::menu_owner(page); i++)
            tap(clock_ms);
        if (go::walked(page)) return;
        press(clock_ms);
        const go::Menu menu = go::menu_for(go::menu_owner(page));
        for (int i = 0; i < menu.row_count && product.screen().editor().focus() != row_for(page);
             i++)
            tap(clock_ms);
        press(clock_ms);
        run(clock_ms, clock_ms + 200);
        clock_ms += 200;
    }

    static go::MenuRow row_for(go::Page page) {
        const go::Menu menu = go::menu_for(go::menu_owner(page));
        for (int i = 0; i < menu.row_count; i++)
            if (go::page_behind(menu.rows[i]) == page) return menu.rows[i];
        return go::MenuRow::kCount;
    }

    void touch_pad(uint32_t& clock_ms, uint32_t held_ms) {
        platform.board_gpio().pad_down = true;
        run(clock_ms, clock_ms + held_ms);
        clock_ms += held_ms;
        platform.board_gpio().pad_down = false;
        run(clock_ms, clock_ms + 100);
        clock_ms += 100;
    }

    // The same moves on the rig's own clock.
    [[nodiscard]] bool bring_receiver_up() { return bring_receiver_up(now_ms); }
    void second(int32_t speed_mm_s, int32_t altitude_msl_m) {
        second(now_ms, speed_mm_s, altitude_msl_m);
    }
    void blind_second() { blind_second(now_ms); }
    void blind_seconds(uint32_t count) { blind_seconds(now_ms, count); }
    void seconds(uint32_t count, int32_t speed_mm_s, int32_t altitude_msl_m) {
        seconds(now_ms, count, speed_mm_s, altitude_msl_m);
    }
    void run_span(uint32_t span_ms) { run_span(now_ms, span_ms); }
    void second_across(int32_t speed_mm_s, int32_t altitude_msl_m) {
        second_across(now_ms, speed_mm_s, altitude_msl_m);
    }
    void press() { press(now_ms); }
    void settle() { settle(now_ms); }
    void tap() { tap(now_ms); }
    void long_touch() { long_touch(now_ms); }
    void show(go::Page page) { show(now_ms, page); }
    void touch_pad(uint32_t held_ms) { touch_pad(now_ms, held_ms); }

    bus::State& state() { return product.state(); }

    // A central connects, and a central goes away, through the platform's own
    // link model. Nothing here reaches into a service: the event travels
    // platform -> board -> bus -> config service, which is the path silicon uses.
    void raise_link(uint16_t session_id = 1) { platform.link().raise_link(session_id); }
    void drop_link() { platform.link().drop_link(); }
    void drop_link(uint16_t session_id, uint8_t reason = 0) {
        platform.link().drop_link(session_id, reason);
    }
    bool link_up() { return product.config_link().config().link_up(); }

    // The last thing the device said on one endpoint, the NMEA broadcast aside, named in JSON.
    std::string last_on(events::Endpoint endpoint) {
        const auto& sent = platform.link().sent;
        for (auto it = sent.rbegin(); it != sent.rend(); ++it)
            if (it->endpoint == endpoint) return link_text(it->bytes);
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
        platform.link().push_rx(link_frame(events::Endpoint::Config, session_id, json));
    }

    // The same app on the log endpoint, which is a separate characteristic and
    // a separate queue.
    void send_log(const char* json) {
        if (!platform.link().up()) raise_link();
        send_log_from(platform.link().session_id(), json);
    }

    void send_log_from(uint16_t session_id, const char* json) {
        platform.link().push_rx(link_frame(events::Endpoint::Log, session_id, json));
    }

    uint32_t utc_offset_s{0};
    uint32_t fix_updates{0};
};

}  // namespace skyblip

#endif
