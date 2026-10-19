#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_KVSTORE_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_KVSTORE_H

#include <string>

#include "ports/kvstore.h"

namespace skyblip::platform::host {

class KeyValueStore : public ports::KeyValueStore {
   public:
    bool ready() const override { return !refuse_mount; }

    Status read(const char* key, uint8_t* out, size_t capacity, size_t& out_bytes) override {
        for (auto& entry : entries_)
            if (entry.used && entry.key == key) {
                if (entry.length > capacity) return Status::OutOfRange;
                for (size_t i = 0; i < entry.length; i++) out[i] = entry.data[i];
                out_bytes = entry.length;
                return Status::Ok;
            }
        return Status::NotFound;
    }

    Status write(const char* key, const uint8_t* data, size_t bytes) override {
        if (refuse_writes) return Status::Full;
        Entry* slot = nullptr;
        for (auto& entry : entries_)
            if (entry.used && entry.key == key) slot = &entry;
        if (!slot)
            for (auto& entry : entries_)
                if (!entry.used) {
                    slot = &entry;
                    slot->key = key;
                    slot->used = true;
                    break;
                }
        if (!slot || bytes > sizeof(slot->data)) return Status::Full;
        for (size_t i = 0; i < bytes; i++) slot->data[i] = data[i];
        slot->length = bytes;
        writes_++;
        return Status::Ok;
    }

    // Counted, not just stored. On the silicon behind this port a write is an
    // NVMC stall on the core that arms PPS-anchored deadlines
    // (core/timing/durable_write.h), so how MANY there are is the thing a test
    // about coalescing has to be able to read.
    uint32_t writes() const { return writes_; }
    bool refuse_writes{false};
    bool refuse_mount{false};

    Status erase(const char* key) override {
        for (auto& entry : entries_)
            if (entry.used && entry.key == key) entry.used = false;
        return Status::Ok;
    }

   private:
    struct Entry {
        bool used{false};
        std::string key;
        uint8_t data[64]{};
        size_t length{0};
    };
    Entry entries_[4];
    uint32_t writes_{0};
};

}  // namespace skyblip::platform::host

#endif
