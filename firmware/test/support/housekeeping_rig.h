// Harness, not a test: the product on the host platform, driven by its button, pad, fixes and link.
#ifndef SKYBLIP_TEST_SUPPORT_HOUSEKEEPING_RIG_H
#define SKYBLIP_TEST_SUPPORT_HOUSEKEEPING_RIG_H

#include <cstdint>
#include <cstring>

#include "core/events/link.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/product.h"
#include "test/support/config_frame.h"

using namespace skyblip;

namespace {

struct HousekeepingRig {
    platform::host::Platform platform;
    go::Product<platform::host::Platform> product{platform};

    explicit HousekeepingRig(ports::Capabilities fitted = platform::host::Platform::kFullyFitted)
        : platform(fitted) {}

    Status setup() { return product.setup(); }

    void run(uint32_t from_ms, uint32_t to_ms, uint32_t step_ms = 50) {
        for (uint32_t at_ms = from_ms; at_ms <= to_ms; at_ms += step_ms) {
            platform.clock().set_milliseconds(at_ms);
            product.run_pass(at_ms);
        }
    }

    uint32_t now_ms{0};

    void run_for(uint32_t span_ms) {
        run(now_ms, now_ms + span_ms);
        now_ms += span_ms;
    }

    // The raw level, held for as long as a thumb would. A long press produces no
    // edges at all, so nothing on the ContactEvent path can see one.
    void hold_button(uint32_t held_ms, bool down = true) {
        platform.board_gpio().button_down = down;
        const uint32_t until = now_ms + held_ms;
        for (; now_ms <= until; now_ms += 50) {
            platform.clock().set_milliseconds(now_ms);
            product.run_pass(now_ms);
        }
    }

    // Held across steps, then released: ui::Button only reports a press once a
    // level has been stable through its debounce window.
    void press() {
        hold_button(80, /*down=*/true);
        hold_button(80, /*down=*/false);
    }

    // The pad the pilot pages with. The page lands on the release.
    void tap() {
        platform.board_gpio().pad_down = true;
        run_for(200);
        platform.board_gpio().pad_down = false;
        run_for(100);
    }

    // One solution from the receiver. core/flight decides what it means and
    // publishes the ADS-L code; nothing here tells the companion link anything.
    void push_fix(int32_t speed_mm_s, int32_t altitude_msl_m) {
        gnss::GnssSolution solution{};
        solution.fix_valid = true;
        solution.speed_mm_s = speed_mm_s;
        solution.altitude_msl_mm = altitude_msl_m * 1000;
        solution.sentences = 1;
        product.bus().gnss.push(solution);
    }

    void on_ground() {
        push_fix(/*speed_mm_s=*/0, /*altitude_msl_m=*/0);
        run_for(200);
    }

    // core/flight wants five seconds of motion it believes before it will call
    // a device airborne, and it throws the first sample away as too jerky, so
    // this is a climb-out rather than one hopeful fix.
    void airborne() {
        for (int i = 0; i < 14; i++) {
            push_fix(/*speed_mm_s=*/50000, /*altitude_msl_m=*/1200);
            run_for(500);
        }
    }

    bus::State& state() { return product.state(); }

    // A companion app talking to the device: the frame arrives on the link the
    // board polls, so it takes the same road a phone's would.
    void send(const char* json) {
        if (!platform.link().up()) platform.link().raise_link();
        platform.link().push_rx(
            link_frame(events::Endpoint::Config, platform.link().session_id(), json));
    }

    common::ConfigService& config() { return product.config_link().config(); }
};

}  // namespace

#endif
