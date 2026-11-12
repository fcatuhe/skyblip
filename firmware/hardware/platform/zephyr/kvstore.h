#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_KVSTORE_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_KVSTORE_H
#if defined(__ZEPHYR__)

#include <errno.h>
#include <zephyr/drivers/flash.h>  // flash_pages_info, flash_get_page_info_by_offs
// NVS moved to the KVSS subsystem in Zephyr 4.4. zephyr/fs/nvs.h is now a
// deprecated shim that warns. The API is identical - same nvs_fs, same nvs_mount
// / nvs_read / nvs_write / nvs_delete - so this is a relocation, not a redesign.
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/printk.h>

#include "hardware/platform/store_mount.h"
#include "ports/kvstore.h"

namespace skyblip::platform::zephyr {

class KeyValueStore : public ports::KeyValueStore {
   public:
    // Mount NVS on the DT-defined `storage_partition`, first erasing what another firmware left
    // there. Returns Ok on success.
    Status begin() {
        // FIXED_PARTITION_* are __DEPRECATED_MACRO aliases in Zephyr 4.4, and the
        // PARTITION_* spellings are the same macros under the intended names.
        file_system_.flash_device = PARTITION_DEVICE(storage_partition);
        if (!device_is_ready(file_system_.flash_device)) return Status::Down;
        file_system_.offset = PARTITION_OFFSET(storage_partition);
        struct flash_pages_info info;
        if (flash_get_page_info_by_offs(file_system_.flash_device, file_system_.offset, &info) != 0)
            return Status::Down;
        file_system_.sector_size = info.size;
        file_system_.sector_count = kSectors;
        Partition partition{file_system_};
        const StoreMount mount = mount_store(partition, kSectors);
        if (mount == StoreMount::ClearedForeignRing)
            printk("NVS: storage_partition held Meshtastic's WarmNodeStore ring, now erased\n");
        if (mount == StoreMount::ClearedRefused)
            printk("NVS: storage_partition was refused, now erased, and what it held is gone\n");
        if (mount == StoreMount::Down) printk("NVS: storage_partition did not mount\n");
        mounted_ = mount != StoreMount::Down;
        return mounted_ ? Status::Ok : Status::Down;
    }

    bool ready() const override { return mounted_; }

    Status read(const char* key, uint8_t* out, size_t capacity, size_t& out_bytes) override {
        if (!mounted_) return Status::Down;
        ssize_t stored = nvs_read(&file_system_, record_id(key), out, capacity);
        if (stored <= 0) return Status::NotFound;
        // INFO: fc 23sep26 nvs_read answers the stored length, and copies only capacity of it
        if (static_cast<size_t>(stored) > capacity) return Status::OutOfRange;
        out_bytes = static_cast<size_t>(stored);
        return Status::Ok;
    }
    Status write(const char* key, const uint8_t* data, size_t bytes) override {
        if (!mounted_) return Status::Down;
        ssize_t written = nvs_write(&file_system_, record_id(key), data, bytes);
        return (written == static_cast<ssize_t>(bytes) || written == 0) ? Status::Ok : Status::Full;
    }
    Status erase(const char* key) override {
        if (!mounted_) return Status::Down;
        return nvs_delete(&file_system_, record_id(key)) == 0 ? Status::Ok : Status::NotFound;
    }

   private:
    static constexpr uint16_t kSectors = 3;

    struct Partition {
        bool read_word(int sector, uint32_t& word) {
            const off_t at =
                file_system.offset + static_cast<off_t>(sector) * file_system.sector_size;
            const int read = flash_read(file_system.flash_device, at, &word, sizeof(word));
            if (read != 0) printk("NVS: reading sector %d failed (%d)\n", sector, read);
            return read == 0;
        }

        bool erase() {
            const int erased = flash_flatten(file_system.flash_device, file_system.offset,
                                             size_t{file_system.sector_size} * kSectors);
            if (erased != 0) printk("NVS: erasing storage_partition failed (%d)\n", erased);
            if (erased == 0) printk("NVS: erased storage_partition\n");
            return erased == 0;
        }

        // INFO: fc 12nov26 -EDEADLK and -ESPIPE are nvs_startup's two refusals of the content
        MountAttempt mount() {
            const int mounted = nvs_mount(&file_system);
            if (mounted == 0) return MountAttempt::Mounted;
            printk("NVS: mount failed (%d)\n", mounted);
            if (mounted == -EDEADLK || mounted == -ESPIPE) return MountAttempt::RefusedContent;
            return MountAttempt::Failed;
        }

        struct nvs_fs& file_system;
    };

    static uint16_t record_id(const char* key) {
        uint32_t hash = 2166136261u;
        for (const char* character = key; *character; ++character)
            hash = (hash ^ static_cast<uint8_t>(*character)) * 16777619u;
        return static_cast<uint16_t>(hash & 0xFFFF) | 1u;  // avoid id 0
    }
    struct nvs_fs file_system_{};
    bool mounted_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
