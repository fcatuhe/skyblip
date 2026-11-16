#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SETTINGS_STORE_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SETTINGS_STORE_H

#include <cstdint>
#include <optional>

#include "core/comms/config.h"
#include "core/identity/activation.h"
#include "ports/capabilities.h"
#include "products/common/alarm.h"
#include "products/common/settings_store.h"
#include "products/skyblip_go/pages/settings_changes.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

static_assert(comms::kPromptDetailBytes >= kSettingsChangesBytes, "the prompt holds every row");

class SettingsStore : public common::SettingsStore {
   public:
    SettingsStore(Settings& settings, const identity::Activation& activation,
                  const std::optional<comms::OwnTransponderHeard>& own_transponder,
                  const ports::Capabilities& fitted = kSounding)
        : settings_(settings),
          activation_(activation),
          own_transponder_(own_transponder),
          fitted_(fitted) {}

    void write_fields(cbor::Writer& writer) const override {
        write_config_fields(writer, settings_, activation_.address());
    }

    void write_default_fields(cbor::Writer& writer) const override {
        write_settings_fields(writer, fitted_defaults());
    }

    Status apply(const cbor::Reader& frame) override {
        Settings next = settings_;
        const Status applied = apply_fitted(next, frame);
        if (applied == Status::Ok) settings_ = next;
        return applied;
    }

    Status describe_changes(const cbor::Reader& frame, char* out, int capacity) const override {
        Settings next = settings_;
        const Status applied = apply_fitted(next, frame);
        if (applied == Status::Ok) (void)describe_settings_changes(settings_, next, out, capacity);
        return applied;
    }

    void restore_defaults() override { settings_ = fitted_defaults(); }

    uint8_t blob_version() const override { return kBlobVersion; }

    Status adopt(const uint8_t* blob, size_t blob_bytes) override {
        Settings loaded;
        const Status read = from_blob(blob, blob_bytes, loaded);
        if (is_ok(read)) settings_ = fitted(loaded);
        return read;
    }

    size_t to_blob(uint8_t* out, size_t capacity) const override {
        if (capacity < blob_size()) return 0;
        go::to_blob(settings_, out, capacity);
        return blob_size();
    }

   private:
    static constexpr ports::Capabilities kSounding =
        ports::Capability::Buzzer | ports::Capability::Haptic;

    // INFO: fc 15nov26 a Go with no buzzer and no haptic can only show an alarm, never sound it
    Settings fitted(Settings settings) const {
        if (!common::can_sound(fitted_) && common::audible(settings.alarm))
            settings.alarm = Alarm::Visual;
        return settings;
    }

    Settings fitted_defaults() const { return fitted(defaults()); }

    Status apply_fitted(Settings& next, const cbor::Reader& frame) const {
        const Status applied = apply_settings(next, frame, comms::address_of(own_transponder_));
        if (applied != Status::Ok) return applied;
        return common::audible(next.alarm) && !common::can_sound(fitted_) ? Status::OutOfRange
                                                                          : Status::Ok;
    }

    Settings& settings_;
    const identity::Activation& activation_;
    const std::optional<comms::OwnTransponderHeard>& own_transponder_;
    const ports::Capabilities& fitted_;
};

}  // namespace skyblip::go

#endif
