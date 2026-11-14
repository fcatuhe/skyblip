#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_DFU_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_DFU_H
#if defined(__ZEPHYR__)

#include <zephyr/devicetree.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/retention/retention.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#include <cstring>

#if defined(CONFIG_SOC_FAMILY_NORDIC_NRF)
#include <hal/nrf_wdt.h>
#endif

#include "core/dfu/image.h"
#include "core/power/shutdown.h"
#include "hardware/platform/zephyr/upload_gate.h"
#include "ports/dfu.h"

// INFO: fc 03oct26 img_mgmt.h drags in bootutil/image.h, off the app include path
struct image_version;
extern "C" int img_mgmt_read_info(int image_slot, struct image_version* version, uint8_t* hash,
                                  uint32_t* flags);

namespace skyblip::platform::zephyr {

// THE TWO MAGICS, and they are a pair. Both are one byte at offset 0 of the
// retention area declared on &gpregret1 in the board devicetree, which is the
// GPREGRET the factory Adafruit bootloader reads on every boot
// (Adafruit_nRF52_Bootloader src/main.c:106-110, check_dfu_mode at :241-270).
// Nothing else in this firmware writes that byte, and 0xA8 / 0xB1 - the
// bootloader's own OTA magics, which would hand control to its unsigned BLE
// update path - are written nowhere in the tree.
//
// 0x57 is deliberate recovery: come up as USB mass storage instead of
// chain-loading MCUboot. It is the SOFTWARE route into the factory bootloader,
// for a device whose reset button is awkward or enclosed. The other route is the
// bootloader's own and works whatever state this firmware is in: a double-click
// on RESET within 0.5 s on the T-Echo, and on the X1, which has no reset button,
// the button held while the cable goes in (boards/seeed/meshtracker_x1/factory/).

// One writer for both. False when the board declares no retention area or the
// device is not ready: the caller decides what that means, and neither caller
// treats it as fatal - a board with no UF2 bootloader has nothing to ask.
inline bool write_boot_magic(uint8_t magic) {
#if DT_NODE_EXISTS(DT_NODELABEL(uf2_boot_retention))
    const struct device* retention = DEVICE_DT_GET(DT_NODELABEL(uf2_boot_retention));
    if (!device_is_ready(retention)) return false;
    return retention_write(retention, 0, &magic, sizeof(magic)) == 0;
#else
    (void)magic;
    return false;
#endif
}

class Dfu : public ports::Dfu {
   public:
    ports::SwapRequest request_swap() override {
        ports::SwapRequest request;
        request.check = check_staged();
        if (request.check == ports::StagedCheck::Matched)
            request.error = static_cast<int16_t>(boot_request_upgrade(BOOT_UPGRADE_TEST));
        request.swap = swap_type(mcuboot_swap_type());
        printk("dfu: slot1 %s, swap request %d, swap type %d\n", check_word(request.check),
               request.error, static_cast<int>(request.swap));
        return request;
    }

    ports::StagedCheck check_staged() override {
        StagedSlot slot;
        return slot.opened() ? dfu::check_image(slot) : ports::StagedCheck::Unreadable;
    }

    void trigger() override { sys_reboot(SYS_REBOOT_WARM); }

    bool confirm() override { return boot_write_img_confirmed() == 0; }
    bool confirmed() override { return boot_is_img_confirmed(); }

    bool running_version(ports::ImageVersion& out) override {
        return read_version(PARTITION_ID(slot0_partition), out);
    }
    bool staged_version(ports::ImageVersion& out) override {
        return read_version(PARTITION_ID(slot1_partition), out);
    }
    bool running_key(ports::SigningKeyHash& out) override {
        return read_key(PARTITION_ID(slot0_partition), out);
    }
    bool staged_key(ports::SigningKeyHash& out) override {
        return read_key(PARTITION_ID(slot1_partition), out);
    }
    bool running_class(dfu::ImageClass& out) {
        uint8_t tlvs[kTlvReadBytes];
        const size_t bytes = read_tlvs(PARTITION_ID(slot0_partition), tlvs);
        return bytes > 0 && dfu::find_image_class(tlvs, bytes, out);
    }
    bool downgrade_allowed() const override {
        return !IS_ENABLED(CONFIG_MCUBOOT_BOOTLOADER_NO_DOWNGRADE);
    }
    bool release_build() const override { return !downgrade_allowed(); }

    bool running_hash(ports::ImageHash& out) override { return read_hash(kPrimarySlot, out); }
    bool staged_hash(ports::ImageHash& out) override { return read_hash(kSecondarySlot, out); }

    void publish_upload_allowed(bool allowed) override { UploadGate::publish(allowed); }
    bool upload_finished() override { return UploadGate::finished(); }
    void forget_upload() override { UploadGate::forget_finished(); }

    // INFO: fc 04sep26 the WDT survives a soft reset, not SYSTEM OFF; it would cut the UF2 session
    ports::RecoveryPath recovery_path() const override {
        return watchdog_running() ? ports::RecoveryPath::PowerOffToFinish
                                  : ports::RecoveryPath::Rebooted;
    }

    ports::RecoveryPath enter_recovery() override {
        if (recovery_path() == ports::RecoveryPath::PowerOffToFinish) {
            recovery_armed_ = true;
            return ports::RecoveryPath::PowerOffToFinish;
        }
        write_boot_magic(power::kUf2MassStorageMagic);
        sys_reboot(SYS_REBOOT_WARM);
        return ports::RecoveryPath::Rebooted;
    }

    static uint8_t boot_magic_for_system_off(power::DfuButton button) {
        return power::boot_magic_for_system_off(button, recovery_armed_);
    }

   private:
    class StagedSlot : public dfu::ImageReader {
       public:
        StagedSlot()
            : opened_(flash_area_open(PARTITION_ID(slot1_partition), &area_) == 0),
              start_(boot_get_image_start_offset(PARTITION_ID(slot1_partition))) {}
        ~StagedSlot() override {
            if (opened_) flash_area_close(area_);
        }

        bool opened() const { return opened_; }
        uint32_t bytes() const override {
            return opened_ && area_->fa_size > start_ ? area_->fa_size - start_ : 0;
        }
        bool read(uint32_t offset, uint8_t* out, size_t bytes) override {
            return flash_area_read(area_, start_ + offset, out, bytes) == 0;
        }

       private:
        const struct flash_area* area_{nullptr};
        bool opened_;
        size_t start_;
    };

    // INFO: fc 13nov26 bootutil_public.h's BOOT_SWAP_TYPE_PANIC, a header off the app include path
    static constexpr int kBootSwapTypePanic = 0xff;

    static ports::SwapType swap_type(int type) {
        switch (type) {
            case BOOT_SWAP_TYPE_NONE: return ports::SwapType::None;
            case BOOT_SWAP_TYPE_TEST: return ports::SwapType::Test;
            case BOOT_SWAP_TYPE_PERM: return ports::SwapType::Permanent;
            case BOOT_SWAP_TYPE_REVERT: return ports::SwapType::Revert;
            case BOOT_SWAP_TYPE_FAIL: return ports::SwapType::Fail;
            case kBootSwapTypePanic: return ports::SwapType::Panic;
            default: return ports::SwapType::Unknown;
        }
    }

    static const char* check_word(ports::StagedCheck check) {
        switch (check) {
            case ports::StagedCheck::Unchecked: return "unchecked";
            case ports::StagedCheck::Matched: return "matches its digest";
            case ports::StagedCheck::Damaged: return "damaged";
            case ports::StagedCheck::Unreadable: return "unreadable";
            case ports::StagedCheck::Undigested: return "has no digest";
        }
        return "unknown";
    }

    static constexpr int kPrimarySlot = 0;
    static constexpr int kSecondarySlot = 1;
    static constexpr size_t kWidestImageHashBytes = 64;

    static bool read_hash(int slot, ports::ImageHash& out) {
        uint8_t hash[kWidestImageHashBytes];
        if (img_mgmt_read_info(slot, nullptr, hash, nullptr) != 0) return false;
        std::memcpy(out.bytes, hash, ports::ImageHash::kBytes);
        return true;
    }

    static bool read_version(uint8_t area_id, ports::ImageVersion& out) {
        mcuboot_img_header header{};
        if (boot_read_bank_header(area_id, &header, sizeof(header)) != 0) return false;
        if (header.mcuboot_version != 1) return false;
        out.major = header.h.v1.sem_ver.major;
        out.minor = header.h.v1.sem_ver.minor;
        out.revision = header.h.v1.sem_ver.revision;
        out.build = header.h.v1.sem_ver.build_num;
        return true;
    }

    // INFO: fc 03oct26 an ECDSA-P256 image's TLV area is 152 bytes, a protected area comes before
    // it | 10nov26 85 bytes once it names its product
    static constexpr size_t kTlvReadBytes = 256;

    static bool read_key(uint8_t area_id, ports::SigningKeyHash& out) {
        uint8_t tlvs[kTlvReadBytes];
        const size_t bytes = read_tlvs(area_id, tlvs);
        return bytes > 0 && dfu::find_key_hash(tlvs, bytes, out);
    }

    static size_t read_tlvs(uint8_t area_id, uint8_t (&tlvs)[kTlvReadBytes]) {
        const struct flash_area* area = nullptr;
        if (flash_area_open(area_id, &area) != 0) return 0;
        const size_t start = boot_get_image_start_offset(area_id);
        uint8_t header_bytes[dfu::kImageHeaderBytes];
        dfu::ImageHeader header;
        size_t read = 0;
        if (flash_area_read(area, start, header_bytes, sizeof(header_bytes)) == 0 &&
            dfu::read_header(header_bytes, sizeof(header_bytes), header)) {
            const size_t at = start + header.tlv_offset();
            const size_t bytes = at < area->fa_size ? MIN(sizeof(tlvs), area->fa_size - at) : 0;
            if (bytes > 0 && flash_area_read(area, at, tlvs, bytes) == 0) read = bytes;
        }
        flash_area_close(area);
        return read;
    }

    static bool watchdog_running() {
#if defined(CONFIG_SOC_FAMILY_NORDIC_NRF) && defined(NRF_WDT)
        return nrf_wdt_started_check(NRF_WDT);
#else
        return false;
#endif
    }

    inline static bool recovery_armed_ = false;
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
