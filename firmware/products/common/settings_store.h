#ifndef SKYBLIP_PRODUCTS_COMMON_SETTINGS_STORE_H
#define SKYBLIP_PRODUCTS_COMMON_SETTINGS_STORE_H

#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/util/cbor.h"
#include "core/util/result.h"

namespace skyblip::common {

class SettingsStore {
   public:
    virtual ~SettingsStore() = default;
    // INFO: fc 27nov26 the store writes the paired tag too, so every key goes in ascending order
    virtual void write_fields(cbor::Writer& writer, std::optional<uint16_t> paired_tag) const = 0;
    virtual void write_default_fields(cbor::Writer& writer) const = 0;
    virtual Status apply(const cbor::Reader& frame) = 0;
    // INFO: fc 03oct26 what apply() would change, rows split by '\n' | 20oct26 or its refusal
    virtual Status describe_changes(const cbor::Reader& frame, char* out, int capacity) const = 0;
    virtual void restore_defaults() = 0;
    virtual uint8_t blob_version() const = 0;
    // INFO: fc 10nov26 a refused blob leaves the settings in use untouched
    virtual Status adopt(const uint8_t* blob, size_t blob_bytes) = 0;
    // INFO: fc 10nov26 0 for a blob wider than capacity, which the link counts as a failed write
    virtual size_t to_blob(uint8_t* out, size_t capacity) const = 0;
};

}  // namespace skyblip::common

#endif
