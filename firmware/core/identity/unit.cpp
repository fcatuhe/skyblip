#include "core/identity/unit.h"

namespace skyblip::identity {

void Unit::load() {
    device_id_ = port_.device_id();
    server_key_id_ = identity::server_key_id(port_.trusted_server_key());
    activation_ = Activation{};
    activation_.bluetooth_tag = port_.bluetooth_tag();
    Ed25519::Seed seed{};
    keyed_ = port_.seed(seed);
    if (!keyed_) return;
    public_key_ = Ed25519::public_key_of(seed);
    UnlockCode stored{};
    if (port_.read_unlock_code(stored) == Status::Ok && unlocks(stored))
        activation_.serial = stored.serial;
}

bool Unit::registers(const Nonce& unit_nonce, const Nonce& server_nonce,
                     const Ed25519::Signature& signature) const {
    return Ed25519::verify(signature, port_.trusted_server_key(),
                           register_message(unit_nonce, server_nonce).bytes());
}

std::optional<Ed25519::Signature> Unit::sign(ConstByteSpan message) const {
    Ed25519::Seed seed{};
    if (!port_.seed(seed)) return std::nullopt;
    return Ed25519::sign(seed, message);
}

bool Unit::unlocks(const UnlockCode& code) const {
    if (!keyed_ || code.serial < kFirstSerial) return false;
    return Ed25519::verify(
        code.signature, port_.trusted_server_key(),
        unlock_message(device_id_, public_key_, code.serial, code.nonce).bytes());
}

Status Unit::store(const UnlockCode& code) {
    const Status written = port_.write_unlock_code(code);
    if (written == Status::Ok) activation_.serial = code.serial;
    return written;
}

}  // namespace skyblip::identity
