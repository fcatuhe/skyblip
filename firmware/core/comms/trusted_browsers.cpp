#include "core/comms/trusted_browsers.h"

#include <cstring>

namespace skyblip::comms {

static_assert(sizeof(TrustedBrowser) == crypto::Ed25519::kPublicKeyBytes + kBrowserLabelBytes + 1,
              "an entry is stored as its bytes, so it must hold no padding");

BrowserId TrustedBrowser::id() const {
    BrowserId id{};
    std::memcpy(id.data(), key.data(), id.size());
    return id;
}

void TrustedBrowsers::store(const crypto::Ed25519::PublicKey& key, const char* label) {
    if (holds(key)) return;
    if (count_ == kCapacity) remove(0);
    TrustedBrowser& entry = entries_[count_++];
    entry = TrustedBrowser{};
    entry.key = key;
    std::strncpy(entry.label, label, kBrowserLabelBytes);
}

bool TrustedBrowsers::forget(const BrowserId& id) {
    for (int index = 0; index < count_; index++) {
        if (entries_[index].id() != id) continue;
        remove(index);
        return true;
    }
    return false;
}

size_t TrustedBrowsers::to_blob(uint8_t* out, size_t capacity) const {
    if (capacity < kBlobBytes) return 0;
    uint8_t payload[kPayloadBytes]{};
    payload[0] = static_cast<uint8_t>(count_);
    std::memcpy(payload + 1, entries_, sizeof(entries_));
    settings::seal(kBlobVersion, payload, sizeof(payload), out, capacity);
    return kBlobBytes;
}

bool TrustedBrowsers::adopt(const uint8_t* blob, size_t bytes) {
    uint8_t payload[kPayloadBytes]{};
    if (bytes != kBlobBytes || settings::blob_version(blob) != kBlobVersion ||
        settings::open(blob, bytes, sizeof(payload), payload) != Status::Ok ||
        payload[0] > kCapacity)
        return false;
    count_ = payload[0];
    std::memcpy(entries_, payload + 1, sizeof(entries_));
    for (TrustedBrowser& entry : entries_) entry.label[kBrowserLabelBytes] = 0;
    return true;
}

int TrustedBrowsers::find(const crypto::Ed25519::PublicKey& key) const {
    for (int index = 0; index < count_; index++)
        if (entries_[index].key == key) return index;
    return -1;
}

void TrustedBrowsers::remove(int index) {
    for (int at = index; at + 1 < count_; at++) entries_[at] = entries_[at + 1];
    entries_[--count_] = TrustedBrowser{};
}

}  // namespace skyblip::comms
