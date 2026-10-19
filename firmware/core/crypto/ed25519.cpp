#include "core/crypto/ed25519.h"

#include "monocypher/monocypher-ed25519.h"

namespace skyblip::crypto {

namespace {
// INFO: fc 18oct26 Monocypher's secret key is seed then public key, and key_pair wipes the seed
struct SecretKey {
    uint8_t bytes[64];
    Ed25519::PublicKey public_key;

    explicit SecretKey(const Ed25519::Seed& seed) {
        Ed25519::Seed spent = seed;
        crypto_ed25519_key_pair(bytes, public_key.data(), spent.data());
    }
};
}  // namespace

Ed25519::PublicKey Ed25519::public_key_of(const Seed& seed) { return SecretKey(seed).public_key; }

Ed25519::Signature Ed25519::sign(const Seed& seed, ConstByteSpan message) {
    const SecretKey key(seed);
    Signature signature{};
    crypto_ed25519_sign(signature.data(), key.bytes, message.data(), message.size());
    return signature;
}

bool Ed25519::verify(const Signature& signature, const PublicKey& key, ConstByteSpan message) {
    return crypto_ed25519_check(signature.data(), key.data(), message.data(), message.size()) == 0;
}

}  // namespace skyblip::crypto
