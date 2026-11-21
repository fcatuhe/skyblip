#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_KVSTORE_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_KVSTORE_H
#if defined(__ZEPHYR__)

#include <errno.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>

#include "hardware/platform/nvs_record.h"
#include "hardware/platform/store_mount.h"
#include "ports/kvstore.h"

namespace skyblip::platform::zephyr {

class KeyValueStore : public ports::KeyValueStore {
   public:
    Status begin() {
        file_system_.flash_device = PARTITION_DEVICE(storage_partition);
        if (!device_is_ready(file_system_.flash_device)) return Status::Down;
        file_system_.offset = PARTITION_OFFSET(storage_partition);
        struct flash_pages_info info;
        if (flash_get_page_info_by_offs(file_system_.flash_device, file_system_.offset, &info) != 0)
            return Status::Down;
        file_system_.sector_size = info.size;
        file_system_.sector_count = kSectors;
        Partition partition{file_system_};
        mounted_ = mount_store(partition, kSectors) != StoreMount::Down;
        return mounted_ ? Status::Ok : Status::Down;
    }

    bool ready() const override { return mounted_; }

    Status read(const char* key, uint8_t* out, size_t capacity, size_t& out_bytes) override {
        if (!mounted_) return Status::Down;
        const ssize_t stored = nvs_read(&file_system_, nvs_record_id(key), out, capacity);
        if (stored == -ENOENT || stored == 0) return Status::NotFound;
        if (stored < 0) return Status::Down;
        // INFO: fc 23sep26 nvs_read answers the stored length, and copies only capacity of it
        if (static_cast<size_t>(stored) > capacity) return Status::OutOfRange;
        out_bytes = static_cast<size_t>(stored);
        return Status::Ok;
    }
    Status write(const char* key, const uint8_t* data, size_t bytes) override {
        if (!mounted_) return Status::Down;
        return status_of(nvs_write(&file_system_, nvs_record_id(key), data, bytes));
    }
    Status erase(const char* key) override {
        if (!mounted_) return Status::Down;
        return status_of(nvs_delete(&file_system_, nvs_record_id(key)));
    }

   private:
    static constexpr uint16_t kSectors = 3;

    // INFO: fc 21nov26 nvs_write answers the length, or 0 if unchanged: only a negative errno fails
    static Status status_of(ssize_t written) {
        if (written == -ENOSPC) return Status::Full;
        return written < 0 ? Status::Down : Status::Ok;
    }

    struct Partition {
        bool read_word(int sector, uint32_t& word) {
            const off_t at =
                file_system.offset + static_cast<off_t>(sector) * file_system.sector_size;
            return flash_read(file_system.flash_device, at, &word, sizeof(word)) == 0;
        }

        bool erase() {
            return flash_flatten(file_system.flash_device, file_system.offset,
                                 size_t{file_system.sector_size} * kSectors) == 0;
        }

        // INFO: fc 12nov26 -EDEADLK and -ESPIPE are nvs_startup's two refusals of the content
        MountAttempt mount() {
            const int mounted = nvs_mount(&file_system);
            if (mounted == 0) return MountAttempt::Mounted;
            if (mounted == -EDEADLK || mounted == -ESPIPE) return MountAttempt::RefusedContent;
            return MountAttempt::Failed;
        }

        struct nvs_fs& file_system;
    };

    struct nvs_fs file_system_{};
    bool mounted_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
