#ifndef SKYBLIP_PRODUCTS_COMMON_SETTINGS_STORE_H
#define SKYBLIP_PRODUCTS_COMMON_SETTINGS_STORE_H

#include <cstddef>
#include <cstdint>

#include "core/comms/config_store.h"
#include "core/util/result.h"

namespace skyblip::common {

class SettingsStore : public comms::ConfigStore {
   public:
    virtual void restore_defaults() = 0;
    virtual uint8_t blob_version() const = 0;
    // INFO: fc 10nov26 a refused blob leaves the settings in use untouched
    virtual Status adopt(const uint8_t* blob, size_t blob_bytes) = 0;
    // INFO: fc 10nov26 0 for a blob wider than capacity, which the link counts as a failed write
    virtual size_t to_blob(uint8_t* out, size_t capacity) const = 0;
};

}  // namespace skyblip::common

#endif
