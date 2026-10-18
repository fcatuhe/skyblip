#if defined(__ZEPHYR__)

#include "hardware/platform/zephyr/identity.h"

#include <hal/nrf_ficr.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(skyblip, LOG_LEVEL_INF);

namespace skyblip::platform::zephyr {

namespace {

const crypto::Ed25519::PublicKey kTrustedServerKey = {
#include "skyblip_server_key.inc"
};

// INFO: fc 18oct26 UICR CUSTOMER: written word by word, never erased, NRFFW[0] shares it
constexpr off_t kCustomerAddress = 0x10001080;
constexpr size_t kSlotBytes = crypto::Ed25519::kSeedBytes;
constexpr uint8_t kErased = 0xFF;
static_assert(Identity::kKeySlots * kSlotBytes == 128, "four seeds fill CUSTOMER exactly");

off_t slot_address(int slot) { return kCustomerAddress + static_cast<off_t>(slot * kSlotBytes); }

}  // namespace

Status Identity::begin() {
    uint8_t id[identity::kDeviceIdBytes] = {0};
    if (hwinfo_get_device_id(id, sizeof(id)) != static_cast<ssize_t>(sizeof(id))) {
        LOG_ERR("identity: no DEVICEID, the unit cannot be activated");
        return Status::Down;
    }
    for (size_t i = 0; i < sizeof(id); i++) device_id_[i] = id[i];
    bluetooth_tag_ = static_cast<uint16_t>(nrf_ficr_deviceaddr_get(NRF_FICR, 0) & 0xFFFFu);
    if (flash_area_open(PARTITION_ID(identity_partition), &page_) != 0) {
        page_ = nullptr;
        LOG_ERR("identity: the page at 0xEA000 did not open, no unlock code can be kept");
    }
    if (!device_is_ready(flash_) || !device_is_ready(entropy_)) {
        LOG_ERR("identity: flash controller or entropy not ready, the unit has no key");
        return Status::Down;
    }
    provision_key();
    return key_slot_ >= 0 ? Status::Ok : Status::Down;
}

// INFO: fc 18oct26 a seed is drawn only into blank slots, and a misread slot is skipped
void Identity::provision_key() {
    key_slot_ = last_filled_slot();
    if (key_slot_ >= 0) return;
    for (int slot = 0; slot < kKeySlots; slot++) {
        crypto::Ed25519::Seed seed{};
        if (entropy_get_entropy(entropy_, seed.data(), static_cast<uint16_t>(seed.size())) != 0) {
            LOG_ERR("identity: the RNG gave no seed, the unit has no key");
            return;
        }
        const int written = flash_write(flash_, slot_address(slot), seed.data(), seed.size());
        if (written == 0 && slot_seed(slot) == seed) {
            key_slot_ = slot;
            LOG_INF("identity: key drawn into UICR slot %d", slot);
            return;
        }
        LOG_ERR("identity: UICR slot %d did not read back as written (%d)", slot, written);
    }
}

int Identity::last_filled_slot() const {
    for (int slot = kKeySlots - 1; slot >= 0; slot--)
        if (slot_filled(slot)) return slot;
    return -1;
}

bool Identity::slot_filled(int slot) const {
    const crypto::Ed25519::Seed seed = slot_seed(slot);
    for (uint8_t byte : seed)
        if (byte != kErased) return true;
    return false;
}

crypto::Ed25519::Seed Identity::slot_seed(int slot) const {
    crypto::Ed25519::Seed seed{};
    seed.fill(kErased);
    if (flash_read(flash_, slot_address(slot), seed.data(), seed.size()) != 0) seed.fill(kErased);
    return seed;
}

bool Identity::seed(crypto::Ed25519::Seed& out) const {
    if (key_slot_ < 0) return false;
    out = slot_seed(key_slot_);
    return true;
}

const crypto::Ed25519::PublicKey& Identity::trusted_server_key() const { return kTrustedServerKey; }

Status Identity::read_unlock_code(identity::UnlockCode& out) {
    if (page_ == nullptr) return Status::Down;
    identity::UnlockPage page{};
    if (flash_area_read(page_, 0, page.data(), page.size()) != 0) return Status::Down;
    const std::optional<identity::UnlockCode> code = identity::code_on(page);
    if (!code) return Status::NotFound;
    out = *code;
    return Status::Ok;
}

// INFO: fc 18oct26 through the flash driver, which syncs each NVMC step with the Bluetooth radio
Status Identity::write_unlock_code(const identity::UnlockCode& code) {
    if (page_ == nullptr) return Status::Down;
    const identity::UnlockPage page = identity::page_of(code);
    if (flash_area_erase(page_, 0, page_->fa_size) != 0) {
        LOG_ERR("identity: the unlock page did not erase");
        return Status::Down;
    }
    if (flash_area_write(page_, 0, page.data(), page.size()) != 0) {
        LOG_ERR("identity: the unlock code was not written");
        return Status::Down;
    }
    identity::UnlockPage back{};
    if (flash_area_read(page_, 0, back.data(), back.size()) != 0 || back != page) {
        LOG_ERR("identity: the unlock code did not read back as written");
        return Status::Down;
    }
    return Status::Ok;
}

bool Identity::draw_random(ByteSpan out) {
    return entropy_get_entropy(entropy_, out.data(), static_cast<uint16_t>(out.size())) == 0;
}

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
