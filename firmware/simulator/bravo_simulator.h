#ifndef SKYBLIP_SIMULATOR_BRAVO_SIMULATOR_H
#define SKYBLIP_SIMULATOR_BRAVO_SIMULATOR_H

#include <deque>
#include <string>

#include "hardware/platform/host/meshtracker_x1.h"
#include "products/skyblip_bravo/product.h"
#include "runtime/tasks.h"
#include "simulator/lamp_look.h"
#include "simulator/phone_stream.h"
#include "simulator/world/world.h"

namespace skyblip::simulator {

class BravoSimulator {
   public:
    using Product = bravo::Product<platform::host::MeshtrackerX1>;

    static constexpr uint32_t kStepMs = 5;

    Status setup() {
        const Status started = product_.setup();
        const Status armed = platform_.watchdog().arm(runtime::kHardwareWatchdogCeilingMs);
        return started == Status::Ok ? armed : started;
    }

    void step(uint32_t now_ms) {
        platform_.clock().set_milliseconds(now_ms);
        world_.step(now_ms, product_.state());
        product_.run_pass(now_ms);
        phone_.read(platform_.link());
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
    World<platform::host::MeshtrackerX1>& world() { return world_; }
    platform::host::MeshtrackerX1& platform() { return platform_; }

    const std::deque<std::string>& sentences() const { return phone_.sentences(); }
    uint32_t sentences_read() const { return phone_.sentences_read(); }
    const PhoneStream& phone() const { return phone_; }

    indication::Lamp lamp_since_last_look() { return lamp_.since_last_look(); }

   private:
    platform::host::MeshtrackerX1 platform_{};
    Product product_{platform_};
    World<platform::host::MeshtrackerX1> world_{platform_};
    Scenario scenario_{};
    PhoneStream phone_{};
    LampLook lamp_{platform_.indicator()};
};

}  // namespace skyblip::simulator

#endif
