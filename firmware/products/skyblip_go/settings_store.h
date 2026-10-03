#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SETTINGS_STORE_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SETTINGS_STORE_H

#include "core/comms/config.h"
#include "core/comms/config_store.h"
#include "products/skyblip_go/pages/settings_changes.h"
#include "products/skyblip_go/settings.h"

namespace skyblip::go {

static_assert(comms::kPromptDetailCap >= kSettingsChangesCap, "the prompt holds every row");

class SettingsStore : public comms::ConfigStore {
   public:
    SettingsStore(Settings& settings, uint32_t device_addr)
        : settings_(settings), device_addr_(device_addr) {}

    void write_fields(json::Writer& w) const override {
        write_json_fields(w, settings_, device_addr_);
    }

    void write_default_fields(json::Writer& w) const override {
        write_json_settings(w, defaults());
    }

    Status apply(const char* json, int len) override { return apply_json(settings_, json, len); }

    int describe_changes(const char* json, int len, char* out, int cap) const override {
        Settings next = settings_;
        if (apply_json(next, json, len) != Status::Ok) return 0;
        return describe_settings_changes(settings_, next, out, cap);
    }

   private:
    Settings& settings_;
    uint32_t device_addr_;
};

}  // namespace skyblip::go

#endif
