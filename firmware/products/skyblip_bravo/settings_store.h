#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SETTINGS_STORE_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SETTINGS_STORE_H

#include "products/common/settings_store.h"
#include "products/skyblip_bravo/settings.h"

namespace skyblip::bravo {

class SettingsStore : public common::SettingsStore {
   public:
    explicit SettingsStore(Settings& settings) : settings_(settings) {}

    void write_fields(cbor::Writer& writer) const override {
        write_settings_fields(writer, settings_);
    }

    void write_default_fields(cbor::Writer& writer) const override {
        write_settings_fields(writer, defaults());
    }

    Status apply(const cbor::Reader& frame) override { return apply_settings(settings_, frame); }

    // INFO: fc 12nov26 no glass to describe a change on, so only the refusal is worked out
    Status describe_changes(const cbor::Reader& frame, char* out, int capacity) const override {
        if (capacity > 0) out[0] = 0;
        Settings next = settings_;
        return apply_settings(next, frame);
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
        bravo::to_blob(settings_, out, capacity);
        return blob_size();
    }

   private:
    Settings& settings_;
};

}  // namespace skyblip::bravo

#endif
