#ifndef SKYBLIP_CORE_CRYPTO_ED25519_H
#define SKYBLIP_CORE_CRYPTO_ED25519_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/util/span.h"

namespace skyblip::crypto {

struct Ed25519 {
    static constexpr size_t kSeedBytes = 32;
    static constexpr size_t kPublicKeyBytes = 32;
    static constexpr size_t kSignatureBytes = 64;

    using Seed = std::array<uint8_t, kSeedBytes>;
    using PublicKey = std::array<uint8_t, kPublicKeyBytes>;
    using Signature = std::array<uint8_t, kSignatureBytes>;

    static PublicKey public_key_of(const Seed& seed);
    static Signature sign(const Seed& seed, ConstByteSpan message);
    static bool verify(const Signature& signature, const PublicKey& key, ConstByteSpan message);
};

}  // namespace skyblip::crypto

#endif
