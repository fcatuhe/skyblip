#ifndef SKYBLIP_CORE_IDENTITY_MESSAGES_H
#define SKYBLIP_CORE_IDENTITY_MESSAGES_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/crypto/ed25519.h"
#include "core/util/span.h"

namespace skyblip::identity {

using crypto::Ed25519;

constexpr size_t kDeviceIdBytes = 8;
constexpr size_t kNonceBytes = 16;
constexpr size_t kServerKeyIdBytes = 4;
// INFO: fc 18oct26 the schema's maxLength for the version, the one field of variable length
constexpr size_t kVersionMaxBytes = 23;

using DeviceId = std::array<uint8_t, kDeviceIdBytes>;
using Nonce = std::array<uint8_t, kNonceBytes>;
using ServerKeyId = std::array<uint8_t, kServerKeyIdBytes>;

ServerKeyId server_key_id(const Ed25519::PublicKey& key);

class Message {
   public:
    static constexpr size_t kCap = 128;

    ConstByteSpan bytes() const { return ConstByteSpan(bytes_, size_); }

    Message& append(ConstByteSpan field);
    Message& append(const char* text);

   private:
    uint8_t bytes_[kCap]{};
    size_t size_{0};
};

Message register_message(const Nonce& unit_nonce, const Nonce& server_nonce);
// INFO: fc 18oct26 release is one byte, 1 for a release build, between the server key id and
// version
Message registration_message(const DeviceId& device_id, const Ed25519::PublicKey& unit_key,
                             const Nonce& unit_nonce, const Nonce& server_nonce,
                             const ServerKeyId& server_key, bool release, const char* version);
Message unlock_message(const DeviceId& device_id, const Ed25519::PublicKey& unit_key,
                       uint16_t serial, const Nonce& unit_nonce);

}  // namespace skyblip::identity

#endif
