#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_IDENTITY_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_IDENTITY_H

#include <cstdint>
#include <deque>
#include <optional>

#include "core/crypto/ed25519.h"
#include "core/identity/messages.h"
#include "core/identity/unlock_code.h"
#include "ports/identity.h"

namespace skyblip::platform::host {

// INFO: fc 18oct26 the unit and test server of schemas/activation.v1.vectors.json, README beside
class Identity : public ports::Identity {
   public:
    using Ed25519 = crypto::Ed25519;

    static constexpr Ed25519::Seed kTestServerSeed = {
        0x5d, 0x23, 0x81, 0x9c, 0x33, 0x00, 0xd8, 0x95, 0xcd, 0x6c, 0xa3,
        0x17, 0x78, 0x9d, 0xb5, 0xcb, 0xa9, 0x2a, 0xda, 0xf4, 0xef, 0x7b,
        0x5f, 0xbe, 0x00, 0xcb, 0xc8, 0xd9, 0x46, 0xd2, 0x43, 0xd5};
    static constexpr Ed25519::PublicKey kTestServerKey = {
        0x58, 0x82, 0xd8, 0xd6, 0xe9, 0x82, 0xe1, 0xe7, 0x17, 0x9d, 0xde,
        0xc5, 0x4a, 0x2c, 0x04, 0xc7, 0x9b, 0x5b, 0x21, 0x6f, 0xb2, 0xc2,
        0x8b, 0xf8, 0x46, 0xd0, 0xfb, 0xe8, 0x20, 0xb5, 0x5a, 0x0f};
    static constexpr Ed25519::Seed kUnitSeed = {0xd9, 0x56, 0x0a, 0x4d, 0xbc, 0x21, 0xc5, 0x75,
                                                0xc3, 0x84, 0x23, 0xa6, 0x74, 0xe6, 0x27, 0x6d,
                                                0x53, 0x33, 0xdc, 0xe8, 0x2e, 0xcb, 0x0f, 0x3a,
                                                0x79, 0xc5, 0x32, 0x83, 0xbb, 0x00, 0x24, 0x5f};
    static constexpr identity::DeviceId kDeviceId = {0x8f, 0x3a, 0x1c, 0x2e,
                                                     0x9b, 0x7d, 0x4f, 0x60};
    static constexpr uint16_t kBluetoothTag = 0x3F2A;
    static constexpr uint16_t kSerial = 0x5AFE;

    static identity::UnlockCode unlock_code_for(uint16_t serial, const identity::Nonce& nonce) {
        identity::UnlockCode code{};
        code.serial = serial;
        code.nonce = nonce;
        code.signature = Ed25519::sign(
            kTestServerSeed,
            identity::unlock_message(kDeviceId, Ed25519::public_key_of(kUnitSeed), serial, nonce)
                .bytes());
        return code;
    }

    explicit Identity(std::optional<uint16_t> serial) {
        if (serial) stored = unlock_code_for(*serial, identity::Nonce{});
    }

    identity::DeviceId device_id() const override { return kDeviceId; }
    uint16_t bluetooth_tag() const override { return kBluetoothTag; }
    bool seed(Ed25519::Seed& out) const override {
        if (!keyed) return false;
        out = kUnitSeed;
        return true;
    }
    const Ed25519::PublicKey& trusted_server_key() const override { return kTestServerKey; }

    Status read_unlock_code(identity::UnlockCode& out) override {
        if (!stored) return Status::NotFound;
        out = *stored;
        return Status::Ok;
    }
    Status write_unlock_code(const identity::UnlockCode& code) override {
        writes++;
        if (write_fails) return Status::Down;
        stored = code;
        return Status::Ok;
    }

    bool draw_random(ByteSpan out) override {
        draws++;
        if (random_fails) return false;
        for (size_t i = 0; i < out.size(); i++) {
            if (queued.empty()) {
                out[i] = static_cast<uint8_t>(counter_++ * 37u + 11u);
            } else {
                out[i] = queued.front();
                queued.pop_front();
            }
        }
        return true;
    }
    void queue_random(const identity::Nonce& nonce) {
        queued.insert(queued.end(), nonce.begin(), nonce.end());
    }

    std::optional<identity::UnlockCode> stored{};
    std::deque<uint8_t> queued{};
    int writes{0};
    int draws{0};
    bool keyed{true};
    bool write_fails{false};
    bool random_fails{false};

   private:
    uint32_t counter_{0};
};

}  // namespace skyblip::platform::host

#endif
