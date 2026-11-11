#ifndef SKYBLIP_CORE_COMMS_CONFIG_STORE_H
#define SKYBLIP_CORE_COMMS_CONFIG_STORE_H

#include "core/util/cbor.h"
#include "core/util/result.h"

namespace skyblip::comms {

class ConfigStore {
   public:
    virtual ~ConfigStore() = default;

    virtual void write_fields(cbor::Writer& writer) const = 0;

    virtual void write_default_fields(cbor::Writer& writer) const = 0;

    virtual Status apply(const cbor::Reader& frame) = 0;

    // INFO: fc 03oct26 what apply() would change, rows split by '\n' | 20oct26 or its refusal
    virtual Status describe_changes(const cbor::Reader& frame, char* out, int capacity) const = 0;
};

}  // namespace skyblip::comms

#endif
