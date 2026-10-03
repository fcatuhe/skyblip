#ifndef SKYBLIP_CORE_COMMS_CONFIG_STORE_H
#define SKYBLIP_CORE_COMMS_CONFIG_STORE_H

#include "core/util/json_min.h"
#include "core/util/result.h"

namespace skyblip::comms {

class ConfigStore {
   public:
    virtual ~ConfigStore() = default;

    virtual void write_fields(json::Writer& w) const = 0;

    virtual void write_default_fields(json::Writer& w) const = 0;

    virtual Status apply(const char* json, int len) = 0;

    // INFO: fc 03oct26 the prompt's detail: what apply() would change, rows split by '\n'
    virtual int describe_changes(const char* json, int len, char* out, int cap) const = 0;
};

}  // namespace skyblip::comms

#endif
