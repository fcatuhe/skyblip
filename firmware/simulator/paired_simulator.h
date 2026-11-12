#ifndef SKYBLIP_SIMULATOR_PAIRED_SIMULATOR_H
#define SKYBLIP_SIMULATOR_PAIRED_SIMULATOR_H

#include <cstdint>
#include <vector>

#include "hardware/platform/host/meshtracker_x1.h"
#include "products/common/services/config_link.h"
#include "products/skyblip_bravo/product.h"
#include "products/skyblip_bravo/settings.h"
#include "products/skyblip_go/settings.h"
#include "runtime/tasks.h"
#include "simulator/lamp_look.h"
#include "simulator/phone_stream.h"
#include "simulator/simulator.h"
#include "simulator/world/bravo_aboard.h"

namespace skyblip::simulator {

// INFO: fc 14nov26 a Go and a Bravo in one aircraft, each pinned to the other before it boots
class PairedSimulator {
   public:
    using Bravo = bravo::Product<platform::host::MeshtrackerX1>;

    static constexpr uint32_t kStepMs = Simulator::kStepMs;
    // INFO: fc 14nov26 test_menu.cpp's own ICAO: a Tango's pilot sets one, so no notice shows
    static constexpr uint32_t kOwnIcao = 0x3C6586;

    Status setup() {
        const Status pinned = pin();
        const Status go = go_.setup();
        const Status bravo = bravo_.setup();
        const Status armed = bravo_platform_.watchdog().arm(runtime::kHardwareWatchdogCeilingMs);
        for (const Status status : {pinned, go, bravo})
            if (status != Status::Ok) return status;
        return armed;
    }

    void step(uint32_t now_ms) {
        bravo_platform_.clock().set_milliseconds(now_ms);
        go_.step(now_ms);
        aboard_.step(now_ms);
        bravo_.run_pass(now_ms);
        phone_.read(bravo_platform_.link());
        bravo_lamp_.watch();
    }

    bool park_refusal(uint32_t now_ms) { return go_.park_refusal(now_ms); }

    void run(uint32_t until_ms, uint32_t step_ms = kStepMs) {
        for (uint32_t now_ms = 0; now_ms <= until_ms; now_ms += step_ms) step(now_ms);
    }

    void load(const Scenario& scenario) {
        go_.load(scenario);
        aboard_.load(scenario);
    }

    bool load_file(const char* path) {
        Scenario scenario;
        if (!load_scenario(path, scenario)) return false;
        load(scenario);
        return true;
    }

    Mode mode() const { return go_.mode(); }
    const Scenario& scenario() const { return go_.scenario(); }

    Simulator& go() { return go_; }
    Simulator::Product& product() { return go_.product(); }
    World<platform::host::Platform>& world() { return go_.world(); }
    indication::Lamp lamp_since_last_look() { return go_.lamp_since_last_look(); }

    Bravo& bravo() { return bravo_; }
    platform::host::MeshtrackerX1& bravo_platform() { return bravo_platform_; }
    BravoAboard& aboard() { return aboard_; }
    const PhoneStream& phone() const { return phone_; }
    indication::Lamp bravo_lamp_since_last_look() { return bravo_lamp_.since_last_look(); }

   private:
    // INFO: fc 14nov26 settings are what a device was found with, so both pins are on flash at boot
    Status pin() {
        go::Settings go_settings = go::defaults();
        go_settings.bravo = PairedLink::kPeripheralAddress;
        go_settings.own_icao = kOwnIcao;
        std::vector<uint8_t> go_blob(go::blob_size());
        go::to_blob(go_settings, go_blob.data(), go_blob.size());

        bravo::Settings bravo_settings = bravo::defaults();
        bravo_settings.peer = bravo::pin_of(PairedLink::kCentralAddress);
        std::vector<uint8_t> bravo_blob(bravo::blob_size());
        bravo::to_blob(bravo_settings, bravo_blob.data(), bravo_blob.size());

        const Status go_stored = go_.platform().key_value().write(
            common::ConfigLinkService::kSettingsKey, go_blob.data(), go_blob.size());
        const Status bravo_stored = bravo_platform_.key_value().write(
            common::ConfigLinkService::kSettingsKey, bravo_blob.data(), bravo_blob.size());
        return go_stored != Status::Ok ? go_stored : bravo_stored;
    }

    Simulator go_{};
    platform::host::MeshtrackerX1 bravo_platform_{};
    Bravo bravo_{bravo_platform_};
    BravoAboard aboard_{go_.world(), bravo_platform_};
    PhoneStream phone_{};
    LampLook bravo_lamp_{bravo_platform_.indicator()};
};

}  // namespace skyblip::simulator

#endif
