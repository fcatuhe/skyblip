#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_IDENTITY_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_IDENTITY_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/storage/flash_map.h>

#include "core/crypto/ed25519.h"
#include "core/identity/messages.h"
#include "core/identity/unlock_code.h"
#include "ports/identity.h"

namespace skyblip::platform::zephyr {

// INFO: fc 18oct26 UICR CUSTOMER key slots and the identity page at 0xEA000, README beside
class Identity : public ports::Identity {
   public:
    static constexpr int kKeySlots = 4;

    Status begin();

    identity::DeviceId device_id() const override { return device_id_; }
    uint16_t bluetooth_tag() const override { return bluetooth_tag_; }
    bool seed(crypto::Ed25519::Seed& out) const override;
    const crypto::Ed25519::PublicKey& trusted_server_key() const override;
    Status read_unlock_code(identity::UnlockCode& out) override;
    Status write_unlock_code(const identity::UnlockCode& code) override;
    bool draw_random(ByteSpan out) override;

    int key_slot() const { return key_slot_; }

   private:
    bool slot_filled(int slot) const;
    crypto::Ed25519::Seed slot_seed(int slot) const;
    int last_filled_slot() const;
    void provision_key();

    const struct device* flash_{DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller))};
    const struct device* entropy_{DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy))};
    const struct flash_area* page_{nullptr};
    identity::DeviceId device_id_{};
    uint16_t bluetooth_tag_{0};
    int key_slot_{-1};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
