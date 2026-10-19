// Harness, not a test: the product on the host platform, driven by its button, pad, fixes and link.
#ifndef SKYBLIP_TEST_SUPPORT_HOUSEKEEPING_RIG_H
#define SKYBLIP_TEST_SUPPORT_HOUSEKEEPING_RIG_H

#include <cstdint>
#include <cstring>

#include "core/events/link.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/product.h"

using namespace skyblip;

namespace {

struct HousekeepingRig {
    platform::host::Platform platform;
    go::Product<platform::host::Platform> product{platform};

    explicit HousekeepingRig(ports::Capabilities fitted = platform::host::Platform::kFullyFitted)
        : platform(fitted) {}

    Status setup() { return product.setup(); }

    void run(uint32_t from_ms, uint32_t to_ms, uint32_t step_ms = 50) {
        for (uint32_t now_ms = from_ms; now_ms <= to_ms; now_ms += step_ms) {
            platform.clock().set_millis(now_ms);
            product.run_pass(now_ms);
        }
    }

    // The raw level, held for as long as a thumb would. A long press produces no
    // edges at all, so nothing on the ContactEvent path can see one.
    void hold_button(uint32_t& now_ms, uint32_t held_ms, bool down = true) {
        platform.board_gpio().button_down = down;
        const uint32_t until = now_ms + held_ms;
        for (; now_ms <= until; now_ms += 50) {
            platform.clock().set_millis(now_ms);
            product.run_pass(now_ms);
        }
    }

    // Held across steps, then released: ui::Button only reports a press once a
    // level has been stable through its debounce window.
    void press(uint32_t& now_ms) {
        hold_button(now_ms, 80, /*down=*/true);
        hold_button(now_ms, 80, /*down=*/false);
    }

    // The authorising gesture: two presses well inside go::ConfirmGesture's
    // window. Each press() above advances the clock by ~200 ms, so the pair
    // lands at a gap a thumb actually produces.
    void double_press(uint32_t& now_ms) {
        press(now_ms);
        press(now_ms);
    }

    // The pad the pilot pages with. The page lands on the release.
    void tap(uint32_t& now_ms) {
        platform.board_gpio().pad_down = true;
        run(now_ms, now_ms + 200);
        now_ms += 200;
        platform.board_gpio().pad_down = false;
        run(now_ms, now_ms + 100);
        now_ms += 100;
    }

    // One solution from the receiver. core/flight decides what it means and
    // publishes the ADS-L code; nothing here tells the companion link anything.
    void push_fix(int32_t speed_mm_s, int32_t alt_msl_m) {
        gnss::GnssSolution solution{};
        solution.fix_valid = true;
        solution.speed_mm_s = speed_mm_s;
        solution.alt_msl_mm = alt_msl_m * 1000;
        solution.sentences = 1;
        product.bus().gnss.push(solution);
    }

    void on_ground(uint32_t& now_ms) {
        push_fix(/*speed_mm_s=*/0, /*alt_msl_m=*/0);
        run(now_ms, now_ms + 200);
        now_ms += 200;
    }

    // core/flight wants five seconds of motion it believes before it will call
    // a device airborne, and it throws the first sample away as too jerky, so
    // this is a climb-out rather than one hopeful fix.
    void airborne(uint32_t& now_ms) {
        for (int i = 0; i < 14; i++) {
            push_fix(/*speed_mm_s=*/50000, /*alt_msl_m=*/1200);
            run(now_ms, now_ms + 500);
            now_ms += 500;
        }
    }

    bus::State& state() { return product.state(); }

    // A companion app talking to the device: the frame arrives on the link the
    // board polls, so it takes the same road a phone's would.
    void send(const char* json) {
        events::RxFrame frame{};
        frame.endpoint = events::Endpoint::Config;
        frame.bytes = static_cast<uint16_t>(std::strlen(json));
        std::memcpy(frame.data.data(), json, frame.bytes);
        platform.link().push_rx(frame);
    }

    comms::ConfigService& config() { return product.config_link().config(); }
};

}  // namespace

#endif
