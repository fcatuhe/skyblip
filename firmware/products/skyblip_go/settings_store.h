#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SETTINGS_STORE_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SETTINGS_STORE_H

#include "core/comms/config.h"
#include "core/comms/config_store.h"
#include "core/identity/activation.h"
#include "products/skyblip_go/pages/settings_changes.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

static_assert(comms::kPromptDetailBytes >= kSettingsChangesBytes, "the prompt holds every row");

class SettingsStore : public comms::ConfigStore {
   public:
    SettingsStore(Settings& settings, const identity::Activation& activation)
        : settings_(settings), activation_(activation) {}

    void write_fields(cbor::Writer& writer) const override {
        write_config_fields(writer, settings_, activation_.address());
    }

    void write_default_fields(cbor::Writer& writer) const override {
        write_settings_fields(writer, defaults());
    }

    Status apply(const cbor::Reader& frame) override { return apply_settings(settings_, frame); }

    int describe_changes(const cbor::Reader& frame, char* out, int capacity) const override {
        Settings next = settings_;
        if (apply_settings(next, frame) != Status::Ok) return 0;
        return describe_settings_changes(settings_, next, out, capacity);
    }

   private:
    Settings& settings_;
    const identity::Activation& activation_;
};

}  // namespace skyblip::go

#endif
