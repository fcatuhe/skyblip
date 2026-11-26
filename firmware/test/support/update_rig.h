// Harness, not a test: a rig on the ground, a receiver with no fix, an install the bootloader runs.
#ifndef SKYBLIP_TEST_SUPPORT_UPDATE_RIG_H
#define SKYBLIP_TEST_SUPPORT_UPDATE_RIG_H

#include <cstdint>
#include <optional>

#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

inline void on_ground(Rig& rig) {
    rig.push_fix(/*altitude_m=*/0, /*updates=*/1);
    rig.run_for(200);
}

inline void receiver_speaks_without_a_fix(Rig& rig) {
    gnss::GnssSolution solution{};
    solution.fix_valid = false;
    solution.sentences = 1;
    rig.product.bus().gnss.push(solution);
}

inline common::ConfigService& config(Rig& rig) { return rig.product.config_link().config(); }

constexpr ports::ImageVersion kRunning{0, 1, 0, 12};
constexpr ports::ImageVersion kStaged{0, 2, 0, 15};

inline void pass(Rig& rig) {
    rig.now_ms += 50;
    rig.run_for(0);
}

inline void open_upload_window(Rig& rig) {
    on_ground(rig);
    rig.send("{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run_for(200);
    config(rig).confirm();
}

inline void stage_versions(Rig& rig) {
    rig.platform.dfu().running_version_valid = true;
    rig.platform.dfu().running_image_version = kRunning;
    rig.platform.dfu().staged_version_valid = true;
    rig.platform.dfu().staged_image_version = kStaged;
}

inline void sign(Rig& rig, uint8_t trusted, uint8_t signer) {
    rig.platform.dfu().running_key_valid = true;
    rig.platform.dfu().running_signer.fill(trusted);
    rig.platform.dfu().staged_key_valid = true;
    rig.platform.dfu().staged_signer.fill(signer);
}

inline void land_upload(Rig& rig) {
    rig.platform.dfu().finished_upload = true;
    pass(rig);
}

inline void install_and_swap(Rig& rig) {
    open_upload_window(rig);
    land_upload(rig);
    rig.run_for(power::kParkSettleMs + power::kReleaseSettleMs + 500);
}

// The device after the bootloader has run: same flash, a fresh boot, and what slot 1 kept.
struct Rebooted {
    Rig rig;
    Rebooted(Rig& before, ports::ImageVersion running, bool confirmed,
             std::optional<ports::ImageVersion> slot1 = std::nullopt,
             ports::Capabilities fitted = platform::host::Platform::kFullyFitted)
        : rig(fitted) {
        rig.platform.key_value() = before.platform.key_value();
        rig.platform.dfu().running_version_valid = true;
        rig.platform.dfu().running_image_version = running;
        rig.platform.dfu().image_confirmed = confirmed;
        rig.platform.dfu().staged_version_valid = slot1.has_value();
        if (slot1) rig.platform.dfu().staged_image_version = *slot1;
    }
};

}  // namespace

#endif
