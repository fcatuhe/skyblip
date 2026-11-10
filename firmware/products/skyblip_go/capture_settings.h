#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_CAPTURE_SETTINGS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_CAPTURE_SETTINGS_H

#include "core/diagnostics/payload.h"
#include "core/settings/address.h"
#include "products/common/services/capture.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

class CaptureSettings final : public common::CaptureSettings {
   public:
    explicit CaptureSettings(const Settings& settings) : settings_(settings) {}
    explicit CaptureSettings(Settings&&) = delete;

    void describe(diagnostics::Config& record) const override {
        record.battery_offset_mv = settings_.battery_offset_mv;
        record.frequency_trim_e1_ppm = settings_.frequency_trim_e1_ppm;
        record.aircraft_type = settings_.aircraft_type;
        record.address_mapping_table = settings::kAddressMappingTable;
        record.alarm_volume = static_cast<uint8_t>(settings_.alarm_volume);
        record.settings_version = settings_.version;
        record.alarm_enabled = settings_.alarm_enabled;
        record.metric = settings_.units == Units::Metric;
        record.battery_trim_manual = settings_.battery_offset_manual;
    }

   private:
    const Settings& settings_;
};

}  // namespace skyblip::go

#endif
