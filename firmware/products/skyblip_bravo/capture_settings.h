#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_CAPTURE_SETTINGS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_CAPTURE_SETTINGS_H

#include "core/diagnostics/payload.h"
#include "products/common/services/capture.h"
#include "products/skyblip_bravo/settings.h"

namespace skyblip::bravo {

// INFO: fc 12nov26 Bravo transmits nothing and trims nothing, so the record's other fields stay 0
class CaptureSettings final : public common::CaptureSettings {
   public:
    explicit CaptureSettings(const Settings& settings) : settings_(settings) {}
    explicit CaptureSettings(Settings&&) = delete;

    void describe(diagnostics::Config& record) const override {
        record.alarm_volume = static_cast<uint8_t>(settings_.alarm_volume);
        record.settings_version = kBlobVersion;
        record.alarm_enabled = settings_.alarm_enabled;
    }

   private:
    const Settings& settings_;
};

}  // namespace skyblip::bravo

#endif
