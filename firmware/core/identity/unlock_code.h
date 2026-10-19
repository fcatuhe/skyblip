#ifndef SKYBLIP_CORE_IDENTITY_UNLOCK_CODE_H
#define SKYBLIP_CORE_IDENTITY_UNLOCK_CODE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/crypto/ed25519.h"
#include "core/identity/messages.h"

namespace skyblip::identity {

// INFO: fc 18oct26 serial 0 would put 0x000000 on the air, which decoders read as no address
constexpr uint16_t kFirstSerial = 1;

struct UnlockCode {
    uint16_t serial{0};
    Nonce nonce{};
    Ed25519::Signature signature{};
};

constexpr size_t kUnlockCodeBytes = 2 + kNonceBytes + Ed25519::kSignatureBytes;
// INFO: fc 18oct26 rounded up to whole 4-byte words, what the nRF52840's NVMC writes
constexpr size_t kUnlockPageBytes = (kUnlockCodeBytes + 3) / 4 * 4;
using UnlockPage = std::array<uint8_t, kUnlockPageBytes>;

UnlockPage page_of(const UnlockCode& code);
std::optional<UnlockCode> code_on(const UnlockPage& page);

}  // namespace skyblip::identity

#endif
