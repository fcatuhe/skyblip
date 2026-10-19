#if defined(__ZEPHYR__)

#include "hardware/platform/zephyr/identity.h"

#include <hal/nrf_ficr.h>
#include <hal/nrf_nvmc.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(skyblip, LOG_LEVEL_INF);

namespace skyblip::platform::zephyr {

namespace {

const crypto::Ed25519::PublicKey kTrustedServerKey = {
#include "skyblip_server_key.inc"
};

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
    if (!device_is_ready(entropy_)) {
        LOG_ERR("identity: no entropy, the unit has no key");
        return Status::Down;
    }
    const std::optional<int> slot = identity::KeySlots::provision(uicr_);
    if (!slot) {
        LOG_ERR("identity: no UICR slot holds a key or took one, the unit has no key");
        return Status::Down;
    }
    LOG_INF("identity: key in UICR slot %d", *slot);
    return Status::Ok;
}

bool Identity::Uicr::read(identity::KeySlots::Words& out) const {
    for (int i = 0; i < identity::KeySlots::kWords; i++) out[i] = NRF_UICR->CUSTOMER[i];
    return true;
}

// INFO: fc 18oct26 NVMC word writes straight to UICR: no driver path here can ever erase it
// INFO: fc 18oct26 interrupts held off for the word, so nothing can reset NVMC CONFIG mid-write
void Identity::Uicr::write(int word, uint32_t value) {
    const unsigned int key = irq_lock();
    while (!nrf_nvmc_ready_check(NRF_NVMC)) {
    }
    nrf_nvmc_mode_set(NRF_NVMC, NRF_NVMC_MODE_WRITE);
    NRF_UICR->CUSTOMER[word] = value;
    __DSB();
    while (!nrf_nvmc_ready_check(NRF_NVMC)) {
    }
    nrf_nvmc_mode_set(NRF_NVMC, NRF_NVMC_MODE_READONLY);
    irq_unlock(key);
}

bool Identity::Uicr::draw(crypto::Ed25519::Seed& out) {
    return entropy_get_entropy(entropy_, out.data(), static_cast<uint16_t>(out.size())) == 0;
}

bool Identity::seed(crypto::Ed25519::Seed& out) const {
    const std::optional<crypto::Ed25519::Seed> seed = identity::KeySlots::seed(uicr_);
    if (!seed) return false;
    out = *seed;
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
