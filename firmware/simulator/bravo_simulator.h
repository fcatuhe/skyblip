#ifndef SKYBLIP_SIMULATOR_BRAVO_SIMULATOR_H
#define SKYBLIP_SIMULATOR_BRAVO_SIMULATOR_H

#include <deque>
#include <string>

#include "core/events/link.h"
#include "hardware/platform/host/meshtracker_x1.h"
#include "products/skyblip_bravo/product.h"
#include "runtime/tasks.h"
#include "simulator/world/world.h"

namespace skyblip::simulator {

class BravoSimulator {
   public:
    using Product = bravo::Product<platform::host::MeshtrackerX1>;

    static constexpr uint32_t kStepMs = 5;
    static constexpr size_t kKeptSentences = 32;

    Status setup() {
        const Status started = product_.setup();
        const Status armed = platform_.watchdog().arm(runtime::kHardwareWatchdogCeilingMs);
        return started == Status::Ok ? armed : started;
    }

    void step(uint32_t now_ms) {
        platform_.clock().set_milliseconds(now_ms);
        world_.step(now_ms, product_.state());
        product_.run_pass(now_ms);
        read_phone();
        if (platform_.indicator().lit()) lit_since_look_ = platform_.indicator().lamp();
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

    // INFO: fc 13nov26 the phone's side of the link: the newest sentences Bravo sent, oldest first
    const std::deque<std::string>& sentences() const { return sentences_; }
    uint32_t sentences_read() const { return sentences_read_; }

    // INFO: fc 09nov26 a 30 ms wink can fall between two browser frames, so a look keeps it
    indication::Lamp lamp_since_last_look() {
        const indication::Lamp seen = lit_since_look_ != indication::Lamp::None
                                          ? lit_since_look_
                                          : platform_.indicator().lamp();
        lit_since_look_ = indication::Lamp::None;
        return seen;
    }

   private:
    // INFO: fc 13nov26 drained every step, so a browser left running for hours holds 32 lines
    void read_phone() {
        for (const auto& frame : platform_.link().sent)
            if (frame.endpoint == events::Endpoint::Nmea) stream_ += frame.bytes;
        platform_.link().clear();
        for (size_t end = stream_.find("\r\n"); end != std::string::npos;
             end = stream_.find("\r\n")) {
            sentences_.push_back(stream_.substr(0, end));
            stream_.erase(0, end + 2);
            sentences_read_++;
            if (sentences_.size() > kKeptSentences) sentences_.pop_front();
        }
    }

    platform::host::MeshtrackerX1 platform_{};
    Product product_{platform_};
    World<platform::host::MeshtrackerX1> world_{platform_};
    Scenario scenario_{};
    std::deque<std::string> sentences_{};
    std::string stream_{};
    uint32_t sentences_read_{0};
    indication::Lamp lit_since_look_{indication::Lamp::None};
};

}  // namespace skyblip::simulator

#endif
