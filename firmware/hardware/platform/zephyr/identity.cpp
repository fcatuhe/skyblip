#if defined(__ZEPHYR__)

#include "hardware/platform/zephyr/identity.h"

#include <hal/nrf_nvmc.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/irq.h>

#include "hardware/platform/zephyr/bluetooth_tag.h"

namespace skyblip::platform::zephyr {

namespace {

const crypto::Ed25519::PublicKey kTrustedServerKey = {
#include "skyblip_server_key.inc"
};

}  // namespace

Status Identity::begin() {
    uint8_t hardware_id[identity::kDeviceIdBytes] = {0};
    if (hwinfo_get_device_id(hardware_id, sizeof(hardware_id)) !=
        static_cast<ssize_t>(sizeof(hardware_id)))
        return Status::Down;
    for (size_t i = 0; i < sizeof(hardware_id); i++) device_id_[i] = hardware_id[i];
    device_id_read_ = true;
    bluetooth_tag_ = zephyr::bluetooth_tag();
    if (flash_area_open(PARTITION_ID(identity_partition), &unlock_page_) != 0)
        unlock_page_ = nullptr;
    return identity::KeySlots::provision(uicr_, device_id_) ? Status::Ok : Status::Down;
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

bool Identity::Uicr::draw(identity::KeySlots::Material& out) {
    return random_.draw(ByteSpan(out.data(), out.size()));
}

bool Identity::seed(crypto::Ed25519::Seed& out) const {
    if (!device_id_read_) return false;
    const std::optional<crypto::Ed25519::Seed> seed = identity::KeySlots::seed(uicr_, device_id_);
    if (!seed) return false;
    out = *seed;
    return true;
}

const crypto::Ed25519::PublicKey& Identity::trusted_server_key() const { return kTrustedServerKey; }

Status Identity::read_unlock_code(identity::UnlockCode& out) {
    if (unlock_page_ == nullptr) return Status::Down;
    identity::UnlockPage page{};
    if (flash_area_read(unlock_page_, 0, page.data(), page.size()) != 0) return Status::Down;
    const std::optional<identity::UnlockCode> code = identity::code_on(page);
    if (!code) return Status::NotFound;
    out = *code;
    return Status::Ok;
}

// INFO: fc 18oct26 through the flash driver, which syncs each NVMC step with the Bluetooth radio
Status Identity::write_unlock_code(const identity::UnlockCode& code) {
    if (unlock_page_ == nullptr) return Status::Down;
    const identity::UnlockPage page = identity::page_of(code);
    if (flash_area_erase(unlock_page_, 0, unlock_page_->fa_size) != 0) return Status::Down;
    if (flash_area_write(unlock_page_, 0, page.data(), page.size()) != 0) return Status::Down;
    identity::UnlockPage read_back{};
    if (flash_area_read(unlock_page_, 0, read_back.data(), read_back.size()) != 0 ||
        read_back != page)
        return Status::Down;
    return Status::Ok;
}

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
