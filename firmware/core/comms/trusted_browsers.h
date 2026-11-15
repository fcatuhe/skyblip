#ifndef SKYBLIP_CORE_COMMS_TRUSTED_BROWSERS_H
#define SKYBLIP_CORE_COMMS_TRUSTED_BROWSERS_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/comms/link_registry.h"
#include "core/crypto/ed25519.h"
#include "core/settings/blob.h"

namespace skyblip::comms {

using BrowserId = std::array<uint8_t, link::browser_bytes::kId>;

constexpr size_t kBrowserLabelBytes = 24;

struct TrustedBrowser {
    crypto::Ed25519::PublicKey key{};
    char label[kBrowserLabelBytes + 1]{};

    BrowserId id() const;
};

class TrustedBrowsers {
   public:
    static constexpr int kCapacity = 8;
    static constexpr uint8_t kBlobVersion = 1;
    static constexpr size_t kPayloadBytes = 1 + kCapacity * sizeof(TrustedBrowser);
    static constexpr size_t kBlobBytes = settings::blob_bytes(kPayloadBytes);

    int count() const { return count_; }
    const TrustedBrowser& at(int index) const { return entries_[index]; }
    bool holds(const crypto::Ed25519::PublicKey& key) const { return find(key) >= 0; }

    // INFO: fc 13nov26 a ninth browser replaces the one stored first, so a lost browser ages out
    void store(const crypto::Ed25519::PublicKey& key, const char* label);
    bool forget(const BrowserId& id);

    size_t to_blob(uint8_t* out, size_t capacity) const;
    bool adopt(const uint8_t* blob, size_t bytes);

   private:
    int find(const crypto::Ed25519::PublicKey& key) const;
    void remove(int index);

    TrustedBrowser entries_[kCapacity]{};
    int count_{0};
};

}  // namespace skyblip::comms

#endif
