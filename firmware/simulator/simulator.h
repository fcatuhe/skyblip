#ifndef SKYBLIP_SIMULATOR_SIMULATOR_H
#define SKYBLIP_SIMULATOR_SIMULATOR_H

#include <cstring>
#include <string>

#include "core/events/link.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/product.h"
#include "runtime/tasks.h"
#include "simulator/lamp_look.h"
#include "simulator/world/world.h"

namespace skyblip::simulator {

class Simulator {
   public:
    using Product = go::Product<platform::host::Platform>;

    Status setup() {
        platform_.retune_gnss_port(true);
        const Status started = product_.setup();
        const Status armed = platform_.watchdog().arm(runtime::kHardwareWatchdogCeilingMs);
        return started == Status::Ok ? armed : started;
    }

    void step(uint32_t now_ms) {
        platform_.clock().set_milliseconds(now_ms);
        world_.step(now_ms, product_.state());
        product_.run_pass(now_ms);
        // INFO: fc 29sep26 on air in the step that keyed it, not the next: 5 ms of a burst's slack
        world_.air().take_own_transmission(platform_.clock().microseconds(),
                                           platform_.chips().radio);
        lamp_.watch();
    }

    bool park_refusal(uint32_t now_ms) {
        platform_.clock().set_milliseconds(now_ms);
        return product_.park_refusal(now_ms);
    }

    // INFO: fc 20nov26 finer than the 5 ms guard on the O to M edge, or the simulation misses a
    // slot boundary the firmware keeps
    static constexpr uint32_t kStepMs = 5;

    void run(uint32_t until_ms, uint32_t step_ms = kStepMs) {
        for (uint32_t now_ms = 0; now_ms <= until_ms; now_ms += step_ms) step(now_ms);
    }

    void load(const Scenario& scenario) {
        scenario_ = scenario;
        world_.load(scenario);
        name_device(scenario.callsign);
    }

    bool load_file(const char* path) {
        Scenario scenario;
        if (!load_scenario(path, scenario)) return false;
        load(scenario);
        return true;
    }

    Mode mode() const { return scenario_.mode; }
    const Scenario& scenario() const { return scenario_; }

    Product& product() { return product_; }
    World<platform::host::Platform>& world() { return world_; }
    platform::host::Platform& platform() { return platform_; }

    // INFO: fc 20nov26 true only once the event travelled platform, board, bus and config service
    bool companion_connected() { return product_.config_link().config().link_up(); }
    int companion_frames(events::Endpoint endpoint) { return platform_.link().count_on(endpoint); }

    const parts::Ssd1681Glass& panel() { return platform_.chips().epd.framebuffer(); }
    bool panel_powered() { return product_.screen().powered(); }
    bool backlight() { return platform_.chips().epd.backlight; }
    int present_count() { return platform_.chips().epd.present_count; }
    bool panel_refreshing() { return panel_driver().refreshing(); }
    bool panel_refresh_is_full() { return panel_driver().refresh_mode() == ports::Refresh::Full; }
    // INFO: fc 20nov26 0 in the gaps of a pattern too
    uint8_t buzzer_level() {
        return platform_.annunciator().sounding() ? traffic::to_number(announcing_level()) : 0;
    }
    // INFO: fc 20nov26 gaps included, so assert on this to ask whether traffic is called out at all
    traffic::Level announcing_level() { return product_.alarm().announcing_level(); }
    indication::Colour lamp_since_last_look() { return lamp_.since_last_look(); }

   private:
    // INFO: fc 21sep26 settings are what a device was found with, not an act inside the scenario
    void name_device(const std::string& callsign) {
        char* out = product_.settings().callsign;
        const size_t bytes =
            callsign.size() < go::kCallsignBytes - 1 ? callsign.size() : go::kCallsignBytes - 1;
        std::memcpy(out, callsign.c_str(), bytes);
        out[bytes] = 0;
    }

    parts::Ssd1681& panel_driver() { return product_.board().display(); }

    platform::host::Platform platform_{};
    Product product_{platform_};
    World<platform::host::Platform> world_{platform_};
    Scenario scenario_{};
    LampLook lamp_{platform_.indicator()};
};

}  // namespace skyblip::simulator

#endif
