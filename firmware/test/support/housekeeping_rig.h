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

using Go = go::Product<platform::host::Platform>;

struct Rig {
    platform::host::Platform platform;
    Go product{platform};

    explicit Rig(ports::Capabilities fitted = platform::host::Platform::kFullyFitted)
        : platform(fitted) {}

    Status setup() { return product.setup(); }

    void run(uint32_t from, uint32_t to, uint32_t step = 50) {
        for (uint32_t t = from; t <= to; t += step) {
            platform.clock().set_millis(t);
            product.step(t);
        }
    }

    // The raw level, held for as long as a thumb would. A long press produces no
    // edges at all, so nothing on the ButtonEvent path can see one.
    void hold_button(uint32_t& t, uint32_t ms, bool down = true) {
        platform.board_gpio().button_down = down;
        const uint32_t until = t + ms;
        for (; t <= until; t += 50) {
            platform.clock().set_millis(t);
            product.step(t);
        }
    }

    // Held across steps, then released: ui::Button only reports a press once a
    // level has been stable through its debounce window.
    void press(uint32_t& t) {
        hold_button(t, 80, /*down=*/true);
        hold_button(t, 80, /*down=*/false);
    }

    // The authorising gesture: two presses well inside go::ConfirmGesture's
    // window. Each press() above advances the clock by ~200 ms, so the pair
    // lands at a gap a thumb actually produces.
    void double_press(uint32_t& t) {
        press(t);
        press(t);
    }

    // The pad the pilot pages with. The page lands on the release.
    void tap(uint32_t& t) {
        platform.board_gpio().pad_down = true;
        run(t, t + 200);
        t += 200;
        platform.board_gpio().pad_down = false;
        run(t, t + 100);
        t += 100;
    }

    // One solution from the receiver. core/flight decides what it means and
    // publishes the ADS-L code; nothing here tells the companion link anything.
    void push_solution(int32_t speed_mm_s, int32_t alt_msl_m) {
        gnss::GnssSolution solution{};
        solution.fix_valid = true;
        solution.speed_mm_s = speed_mm_s;
        solution.alt_msl_mm = alt_msl_m * 1000;
        solution.updates = 1;
        product.bus().gnss.push(solution);
    }

    void on_ground(uint32_t& t) {
        push_solution(/*speed_mm_s=*/0, /*alt_msl_m=*/0);
        run(t, t + 200);
        t += 200;
    }

    // core/flight wants five seconds of motion it believes before it will call
    // a device airborne, and it throws the first sample away as too jerky, so
    // this is a climb-out rather than one hopeful fix.
    void airborne(uint32_t& t) {
        for (int i = 0; i < 14; i++) {
            push_solution(/*speed_mm_s=*/50000, /*alt_msl_m=*/1200);
            run(t, t + 500);
            t += 500;
        }
    }

    bus::State& state() { return product.state(); }

    // A companion app talking to the device: the frame arrives on the link the
    // board polls, so it takes the same road a phone's would.
    void send(const char* json) {
        events::RxFrame frame{};
        frame.endpoint = events::Endpoint::Config;
        frame.len = static_cast<uint16_t>(std::strlen(json));
        std::memcpy(frame.data.data(), json, frame.len);
        platform.link().push_rx(frame);
    }

    comms::ConfigService& config() { return product.config().config(); }
};

}  // namespace

#endif
