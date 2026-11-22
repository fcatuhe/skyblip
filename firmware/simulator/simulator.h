#ifndef SKYBLIP_SIMULATOR_SIMULATOR_H
#define SKYBLIP_SIMULATOR_SIMULATOR_H

#include <cstring>
#include <string>

#include "core/events/link.h"
#include "hardware/platform/host/platform.h"
#include "products/skyblip_go/product.h"
#include "simulator/unit_simulator.h"

namespace skyblip::simulator {

class Simulator : public UnitSimulator<Simulator, platform::host::Platform,
                                       go::Product<platform::host::Platform>> {
   public:
    using Product = go::Product<platform::host::Platform>;

    Status setup() {
        platform_.retune_gnss_port(true);
        return UnitSimulator::setup();
    }

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

   private:
    friend UnitSimulator;

    // INFO: fc 29sep26 on air in the step that keyed it, not the next: 5 ms of a burst's slack
    void after_pass() {
        world_.air().take_own_transmission(platform_.clock().microseconds(),
                                           platform_.chips().radio);
    }

    void after_load() { name_device(scenario_.callsign); }

    // INFO: fc 21sep26 settings are what a device was found with, not an act inside the scenario
    void name_device(const std::string& callsign) {
        char* out = product_.settings().callsign;
        const size_t bytes =
            callsign.size() < go::kCallsignBytes - 1 ? callsign.size() : go::kCallsignBytes - 1;
        std::memcpy(out, callsign.c_str(), bytes);
        out[bytes] = 0;
    }

    parts::Ssd1681& panel_driver() { return product_.board().display(); }
};

}  // namespace skyblip::simulator

#endif
