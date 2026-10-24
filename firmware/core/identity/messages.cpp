#include "core/identity/messages.h"

#include "core/util/sha256.h"

namespace skyblip::identity {

namespace {
constexpr const char* kRegisterLabel = "skyblip:register:v1";
constexpr const char* kRegistrationLabel = "skyblip:registration:v1";
constexpr const char* kUnlockLabel = "skyblip:unlock:v1";

static_assert(sizeof("skyblip:registration:v1") - 1 + kDeviceIdBytes + Ed25519::kPublicKeyBytes +
                      2 * kNonceBytes + kServerKeyIdBytes + 1 + kMaxVersionBytes <=
                  Message::kBytes,
              "the longest message, the registration, fits whole");

template <size_t Bytes>
ConstByteSpan span_of(const std::array<uint8_t, Bytes>& bytes) {
    return ConstByteSpan(bytes.data(), Bytes);
}
}  // namespace

ServerKeyId server_key_id(const Ed25519::PublicKey& key) {
    const Sha256::Digest digest = Sha256::of(key.data(), key.size());
    ServerKeyId id{};
    for (size_t i = 0; i < id.size(); i++) id[i] = digest[i];
    return id;
}

Message& Message::append(ConstByteSpan field) {
    for (size_t i = 0; i < field.size() && size_ < kBytes; i++) bytes_[size_++] = field[i];
    return *this;
}

Message& Message::append(const char* text) {
    size_t text_bytes = 0;
    while (text[text_bytes] != 0) text_bytes++;
    return append(ConstByteSpan(reinterpret_cast<const uint8_t*>(text), text_bytes));
}

Message register_message(const Nonce& unit_nonce, const Nonce& server_nonce) {
    Message message;
    message.append(kRegisterLabel).append(span_of(unit_nonce)).append(span_of(server_nonce));
    return message;
}

Message registration_message(const DeviceId& device_id, const Ed25519::PublicKey& unit_key,
                             const Nonce& unit_nonce, const Nonce& server_nonce,
                             const ServerKeyId& server_key_id, bool release, const char* version) {
    const uint8_t release_byte = release ? 1 : 0;
    size_t version_bytes = 0;
    while (version_bytes < kMaxVersionBytes && version[version_bytes] != 0) version_bytes++;
    Message message;
    message.append(kRegistrationLabel)
        .append(span_of(device_id))
        .append(span_of(unit_key))
        .append(span_of(unit_nonce))
        .append(span_of(server_nonce))
        .append(span_of(server_key_id))
        .append(ConstByteSpan(&release_byte, 1))
        .append(ConstByteSpan(reinterpret_cast<const uint8_t*>(version), version_bytes));
    return message;
}

Message unlock_message(const DeviceId& device_id, const Ed25519::PublicKey& unit_key,
                       uint16_t serial, const Nonce& unit_nonce) {
    const uint8_t serial_big_endian[2] = {static_cast<uint8_t>(serial >> 8),
                                          static_cast<uint8_t>(serial & 0xFF)};
    Message message;
    message.append(kUnlockLabel)
        .append(span_of(device_id))
        .append(span_of(unit_key))
        .append(ConstByteSpan(serial_big_endian, 2))
        .append(span_of(unit_nonce));
    return message;
}

}  // namespace skyblip::identity
