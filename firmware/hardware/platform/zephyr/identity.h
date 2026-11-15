#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_IDENTITY_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_IDENTITY_H
#if defined(__ZEPHYR__)

#include <zephyr/device.h>
#include <zephyr/storage/flash_map.h>

#include "core/crypto/ed25519.h"
#include "core/identity/key_slots.h"
#include "core/identity/messages.h"
#include "core/identity/unlock_code.h"
#include "hardware/platform/zephyr/random.h"
#include "ports/identity.h"

namespace skyblip::platform::zephyr {

// INFO: fc 18oct26 UICR CUSTOMER key slots and the identity page at 0xEA000, README beside
class Identity : public ports::Identity {
   public:
    explicit Identity(Random& random) : uicr_(random) {}

    Status begin();

    identity::DeviceId device_id() const override { return device_id_; }
    uint16_t bluetooth_tag() const override { return bluetooth_tag_; }
    bool seed(crypto::Ed25519::Seed& out) const override;
    const crypto::Ed25519::PublicKey& trusted_server_key() const override;
    Status read_unlock_code(identity::UnlockCode& out) override;
    Status write_unlock_code(const identity::UnlockCode& code) override;

   private:
    class Uicr : public identity::KeySlots::Memory {
       public:
        explicit Uicr(Random& random) : random_(random) {}
        bool read(identity::KeySlots::Words& out) const override;
        void write(int word, uint32_t value) override;
        bool draw(identity::KeySlots::Material& out) override;

       private:
        Random& random_;
    };

    Uicr uicr_;
    const struct flash_area* unlock_page_{nullptr};
    identity::DeviceId device_id_{};
    uint16_t bluetooth_tag_{0};
    bool device_id_read_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
