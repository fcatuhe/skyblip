#ifndef SKYBLIP_SIMULATOR_UNIT_SIMULATOR_H
#define SKYBLIP_SIMULATOR_UNIT_SIMULATOR_H

#include "simulator/lamp_look.h"
#include "simulator/world/world.h"

namespace skyblip::simulator {

template <class Unit, class Platform, class Product>
class UnitSimulator {
   public:
    // INFO: fc 20nov26 finer than the 5 ms guard on the O to M edge, or the simulation misses a
    // slot boundary the firmware keeps
    static constexpr uint32_t kStepMs = 5;

    Status setup() {
        const Status started = product_.setup();
        const Status armed = platform_.watchdog().arm();
        return started == Status::Ok ? armed : started;
    }

    void step(uint32_t now_ms) {
        platform_.clock().set_milliseconds(now_ms);
        world_.step(now_ms, product_.state());
        product_.run_pass(now_ms);
        static_cast<Unit&>(*this).after_pass();
        lamp_.watch();
    }

    bool park_refusal(uint32_t now_ms) {
        platform_.clock().set_milliseconds(now_ms);
        return product_.park_refusal(now_ms);
    }

    void run(uint32_t until_ms, uint32_t step_ms = kStepMs) {
        for (uint32_t now_ms = 0; now_ms <= until_ms; now_ms += step_ms) step(now_ms);
    }

    void load(const Scenario& scenario) {
        scenario_ = scenario;
        world_.load(scenario);
        static_cast<Unit&>(*this).after_load();
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
    World<Platform>& world() { return world_; }
    Platform& platform() { return platform_; }

    indication::Colour lamp_since_last_look() { return lamp_.since_last_look(); }

   protected:
    Platform platform_{};
    Product product_{platform_};
    World<Platform> world_{platform_};
    Scenario scenario_{};
    LampLook lamp_{platform_.indicator()};
};

}  // namespace skyblip::simulator

#endif
