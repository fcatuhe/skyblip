#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SETTINGS_STORE_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SETTINGS_STORE_H

#include "core/comms/config.h"
#include "core/identity/activation.h"
#include "products/common/settings_store.h"
#include "products/skyblip_go/pages/settings_changes.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

static_assert(comms::kPromptDetailBytes >= kSettingsChangesBytes, "the prompt holds every row");

class SettingsStore : public common::SettingsStore {
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

    Status describe_changes(const cbor::Reader& frame, char* out, int capacity) const override {
        Settings next = settings_;
        const Status applied = apply_settings(next, frame);
        if (applied == Status::Ok) (void)describe_settings_changes(settings_, next, out, capacity);
        return applied;
    }

    bool pilot_settings_only(const cbor::Reader& frame) const override {
        return go::pilot_settings_only(frame);
    }

    void restore_defaults() override { settings_ = defaults(); }

    uint8_t blob_version() const override { return kBlobVersion; }

    Status adopt(const uint8_t* blob, size_t blob_bytes) override {
        Settings loaded;
        const Status read = from_blob(blob, blob_bytes, loaded);
        if (is_ok(read)) settings_ = loaded;
        return read;
    }

    size_t to_blob(uint8_t* out, size_t capacity) const override {
        if (capacity < blob_size()) return 0;
        go::to_blob(settings_, out, capacity);
        return blob_size();
    }

   private:
    Settings& settings_;
    const identity::Activation& activation_;
};

}  // namespace skyblip::go

#endif
