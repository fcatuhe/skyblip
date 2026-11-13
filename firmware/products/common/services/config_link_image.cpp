#include "products/common/services/config_link.h"

#include <optional>

namespace skyblip::common {

void ConfigLinkService::load_image_state() {
    if (image_state_loaded_) return;
    image_state_loaded_ = true;
    const ports::Capabilities fitted = context_.roles.capabilities;
    const bool dfu_fitted = ports::has(fitted, ports::Capability::Dfu);
    update_recorded_ = false;
    if (storable()) {
        uint8_t blob[dfu::kUpdateRecordBytes];
        size_t blob_bytes = 0;
        update_recorded_ =
            is_ok(context_.roles.key_value.read(kUpdateKey, blob, sizeof(blob), blob_bytes)) &&
            dfu::from_blob(blob, blob_bytes, update_record_);
    }
    image_confirmed_ = !dfu_fitted || context_.roles.dfu.confirmed();
    image_state_ = image_confirmed_ ? dfu::ImageState::Confirmed : dfu::ImageState::Probation;

    dfu::SlotImage running;
    if (update_recorded_) {
        if (!dfu_fitted || !context_.roles.dfu.running_version(running.version)) {
            forget_update();
        } else {
            running.hashed = context_.roles.dfu.running_hash(running.hash);
            std::optional<dfu::SlotImage> staged;
            dfu::SlotImage held;
            if (context_.roles.dfu.staged_version(held.version)) {
                held.hashed = context_.roles.dfu.staged_hash(held.hash);
                staged = held;
            }
            switch (dfu::outcome(update_record_, running, staged)) {
                case dfu::Outcome::Reverted:
                    if (image_confirmed_) image_state_ = dfu::ImageState::Reverted;
                    break;
                case dfu::Outcome::Refused:
                    if (image_confirmed_) image_state_ = dfu::ImageState::Refused;
                    break;
                case dfu::Outcome::Landed:
                    if (image_confirmed_) forget_update();
                    break;
                case dfu::Outcome::Unrelated: forget_update(); break;
            }
        }
    }
    publish_image_state();
}

void ConfigLinkService::record_update() {
    if (!storable() || !ports::has(context_.roles.capabilities, ports::Capability::Dfu)) return;
    dfu::UpdateRecord record;
    if (!context_.roles.dfu.running_version(record.from)) return;
    if (!context_.roles.dfu.staged_version(record.to)) return;
    record.hashed = context_.roles.dfu.running_hash(record.from_hash) &&
                    context_.roles.dfu.staged_hash(record.to_hash);
    uint8_t blob[dfu::kUpdateRecordBytes];
    const size_t blob_bytes = dfu::to_blob(record, blob, sizeof(blob));
    if (!is_ok(context_.roles.key_value.write(kUpdateKey, blob, blob_bytes))) return;
    update_record_ = record;
    update_recorded_ = true;
    keep_settings_for_a_revert();
}

// INFO: fc 26sep26 a snapshot that failed costs only the restore, and the reverted image says
// defaults
void ConfigLinkService::keep_settings_for_a_revert() {
    uint8_t blob[kBlobBytes];
    const size_t length = store_.to_blob(blob, sizeof(blob));
    if (!blob_fits(length)) {
        failed_++;
        return;
    }
    if (!is_ok(context_.roles.key_value.write(kPriorSettingsKey, blob, length))) return;
    if (newer_kept_) remember(blob, length);
}

void ConfigLinkService::forget_update() {
    // INFO: fc 23sep26 a failed erase leaves the record for the next boot, which forgets it again
    if (update_recorded_) (void)context_.roles.key_value.erase(kUpdateKey);
    update_recorded_ = false;
    update_record_ = dfu::UpdateRecord{};
}

void ConfigLinkService::publish_image_state() {
    config_.set_image_state(image_state_, update_record_);
}

// INFO: fc 07sep26 a solution need not be a fix: an RMC with status V still proves the UART
bool ConfigLinkService::hardware_proven() const {
    const bus::State& state = context_.state;
    if (!state.started || state.flight.gnss_solutions == 0) return false;
    // INFO: fc 03oct26 a slim image that cannot boot its hub is reverted, to the image that could
    if (!state.imu.bootable) return false;
    if (!ports::has(context_.roles.capabilities, ports::Capability::Display)) return true;
    return state.panel_presented;
}

void ConfigLinkService::confirm_image_once_healthy() {
    if (image_confirmed_ || !hardware_proven()) return;
    if (confirm_attempts_ >= kConfirmAttempts) return;
    confirm_attempts_++;
    if (!context_.roles.dfu.confirm()) return;
    image_confirmed_ = true;
    image_state_ = dfu::ImageState::Confirmed;
    forget_update();
    publish_image_state();
}

}  // namespace skyblip::common
