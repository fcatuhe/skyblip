#include "core/identity/unlock_code.h"

namespace skyblip::identity {

namespace {
constexpr uint8_t kErased = 0xFF;
constexpr size_t kNonceAt = 2;
constexpr size_t kSignatureAt = kNonceAt + kNonceBytes;
}  // namespace

UnlockPage page_of(const UnlockCode& code) {
    UnlockPage page{};
    page.fill(kErased);
    page[0] = static_cast<uint8_t>(code.serial >> 8);
    page[1] = static_cast<uint8_t>(code.serial & 0xFF);
    for (size_t i = 0; i < kNonceBytes; i++) page[kNonceAt + i] = code.nonce[i];
    for (size_t i = 0; i < Ed25519::kSignatureBytes; i++)
        page[kSignatureAt + i] = code.signature[i];
    return page;
}

std::optional<UnlockCode> code_on(const UnlockPage& page) {
    bool erased = true;
    for (size_t i = 0; i < kUnlockCodeBytes; i++) erased = erased && page[i] == kErased;
    if (erased) return std::nullopt;
    UnlockCode code{};
    code.serial = static_cast<uint16_t>((page[0] << 8) | page[1]);
    for (size_t i = 0; i < kNonceBytes; i++) code.nonce[i] = page[kNonceAt + i];
    for (size_t i = 0; i < Ed25519::kSignatureBytes; i++)
        code.signature[i] = page[kSignatureAt + i];
    return code;
}

}  // namespace skyblip::identity
