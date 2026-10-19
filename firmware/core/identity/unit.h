#ifndef SKYBLIP_CORE_IDENTITY_UNIT_H
#define SKYBLIP_CORE_IDENTITY_UNIT_H

#include <cstdint>
#include <optional>

#include "core/crypto/ed25519.h"
#include "core/identity/activation.h"
#include "core/identity/messages.h"
#include "core/identity/unlock_code.h"
#include "core/util/result.h"
#include "core/util/span.h"
#include "ports/identity.h"

namespace skyblip::identity {

class Unit {
   public:
    explicit Unit(ports::Identity& port) : port_(port) {}

    void load();

    bool key_provisioned() const { return key_provisioned_; }
    const DeviceId& device_id() const { return device_id_; }
    const Ed25519::PublicKey& public_key() const { return public_key_; }
    const ServerKeyId& server_key_id() const { return server_key_id_; }
    Activation activation() const { return activation_; }

    bool draw_nonce(Nonce& out) { return port_.draw_random(ByteSpan(out.data(), out.size())); }
    bool registers(const Nonce& unit_nonce, const Nonce& server_nonce,
                   const Ed25519::Signature& signature) const;
    std::optional<Ed25519::Signature> sign(ConstByteSpan message) const;
    bool unlocks(const UnlockCode& code) const;
    Status store(const UnlockCode& code);

   private:
    ports::Identity& port_;
    DeviceId device_id_{};
    Ed25519::PublicKey public_key_{};
    ServerKeyId server_key_id_{};
    Activation activation_{};
    bool key_provisioned_{false};
};

}  // namespace skyblip::identity

#endif
