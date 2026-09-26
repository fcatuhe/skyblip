// ports/kvstore.h: capability port: durable key/value blob storage (config
#ifndef SKYBLIP_PORTS_KVSTORE_H
#define SKYBLIP_PORTS_KVSTORE_H

#include <cstddef>
#include <cstdint>

#include "core/util/result.h"

namespace skyblip::ports {

class KvStore {
   public:
    virtual ~KvStore() = default;

    // INFO: fc 25sep26 as FlashRegion::ready(): Storage says meant to have, this says came up
    virtual bool ready() const = 0;
    virtual Status read(const char* key, uint8_t* buf, size_t cap, size_t& out_len) = 0;
    virtual Status write(const char* key, const uint8_t* buf, size_t len) = 0;
    virtual Status erase(const char* key) = 0;
};

}

#endif
