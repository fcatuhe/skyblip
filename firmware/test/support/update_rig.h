// Harness, not a test: a rig on the ground, a receiver with no fix, an install the bootloader runs.
#ifndef SKYBLIP_TEST_SUPPORT_UPDATE_RIG_H
#define SKYBLIP_TEST_SUPPORT_UPDATE_RIG_H

#include <cstdint>
#include <optional>

#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

inline void on_ground(Rig& rig, uint32_t& now_ms) {
    rig.push_fix(/*altitude_m=*/0, /*updates=*/1);
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
}

inline void receiver_speaks_without_a_fix(Rig& rig) {
    gnss::GnssSolution solution{};
    solution.fix_valid = false;
    solution.sentences = 1;
    rig.product.bus().gnss.push(solution);
}

inline comms::ConfigService& config(Rig& rig) { return rig.product.config_link().config(); }

constexpr ports::ImageVersion kRunning{0, 1, 0, 12};
constexpr ports::ImageVersion kStaged{0, 2, 0, 15};

inline void pass(Rig& rig, uint32_t& now_ms) {
    now_ms += 50;
    rig.run(now_ms, now_ms);
}

inline void open_upload_window(Rig& rig, uint32_t& now_ms) {
    on_ground(rig, now_ms);
    rig.send("{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
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

inline void land_upload(Rig& rig, uint32_t& now_ms) {
    rig.platform.dfu().finished_upload = true;
    pass(rig, now_ms);
}

inline void install_and_swap(Rig& rig) {
    uint32_t now_ms = 0;
    open_upload_window(rig, now_ms);
    land_upload(rig, now_ms);
    rig.run(now_ms, now_ms + power::kParkSettleMs + power::kReleaseSettleMs + 500);
}

// The device after the bootloader has run: same flash, a fresh boot, and what slot 1 kept.
struct Rebooted {
    Rig rig;
    Rebooted(Rig& before, ports::ImageVersion running, bool confirmed,
             std::optional<ports::ImageVersion> slot1 = std::nullopt) {
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
