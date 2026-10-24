#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_IDENTITY_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_IDENTITY_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/storage/flash_map.h>

#include "core/crypto/ed25519.h"
#include "core/identity/key_slots.h"
#include "core/identity/messages.h"
#include "core/identity/unlock_code.h"
#include "ports/identity.h"

namespace skyblip::platform::zephyr {

// INFO: fc 18oct26 UICR CUSTOMER key slots and the identity page at 0xEA000, README beside
class Identity : public ports::Identity {
   public:
    Status begin();

    identity::DeviceId device_id() const override { return device_id_; }
    uint16_t bluetooth_tag() const override { return bluetooth_tag_; }
    bool seed(crypto::Ed25519::Seed& out) const override;
    const crypto::Ed25519::PublicKey& trusted_server_key() const override;
    Status read_unlock_code(identity::UnlockCode& out) override;
    Status write_unlock_code(const identity::UnlockCode& code) override;
    bool draw_random(ByteSpan out) override;

   private:
    class Uicr : public identity::KeySlots::Memory {
       public:
        explicit Uicr(const struct device* entropy) : entropy_(entropy) {}
        bool read(identity::KeySlots::Words& out) const override;
        void write(int word, uint32_t value) override;
        bool draw(crypto::Ed25519::Seed& out) override;

       private:
        const struct device* entropy_;
    };

    const struct device* entropy_{DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy))};
    Uicr uicr_{entropy_};
    const struct flash_area* unlock_page_{nullptr};
    identity::DeviceId device_id_{};
    uint16_t bluetooth_tag_{0};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
