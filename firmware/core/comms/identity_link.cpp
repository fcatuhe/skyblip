#include "core/comms/identity_link.h"

#include "core/comms/config.h"
#include "core/dfu/update.h"
#include "core/util/cbor.h"

namespace skyblip::comms {

namespace {
constexpr uint32_t kSerialCeiling = 0xFFFF;

template <size_t Width, size_t Bytes>
bool read_bytes(const cbor::Reader& frame, uint32_t key, std::array<uint8_t, Bytes>& out) {
    static_assert(Width == Bytes, "the registry fixes the width the identity type holds");
    return frame.read_bytes(key, ByteSpan(out.data(), Bytes)) == Status::Ok;
}

template <size_t Width, size_t Bytes>
void add_bytes(cbor::Writer& writer, uint32_t key, const std::array<uint8_t, Bytes>& bytes) {
    static_assert(Width == Bytes, "the registry fixes the width the identity type holds");
    writer.add_bytes(key, ConstByteSpan(bytes.data(), Bytes));
}
}  // namespace

// INFO: fc 18oct26 the server refuses a registration with no version, so none is ever sent
bool ConfigService::running_version_text(char* out, size_t capacity) const {
    ports::ImageVersion running;
    if (dfu_ == nullptr || !dfu_->running_version(running)) return false;
    return dfu::format_version(running, out, capacity) > 0;
}

bool IdentityLink::open(uint16_t session_id, uint32_t now_ms) {
    close();
    if (!unit_.draw_nonce(unit_nonce_)) return false;
    session_id_ = session_id;
    opened_ms_ = now_ms;
    open_ = true;
    return true;
}

// INFO: fc 18oct26 each refusal is refuse(link::Reason::Word): the activation page greps them
bool ConfigService::answer_identity(link::Command command, const cbor::Reader& frame) {
    if (identity_ == nullptr) return false;
    if (command == link::Command::Challenge) {
        challenge();
    } else if (command == link::Command::Register) {
        register_unit(frame);
    } else if (command == link::Command::Unlock) {
        unlock(frame);
    } else {
        return false;
    }
    return true;
}

// INFO: fc 18oct26 an unactivated unit is silent and a bench has no fix: only activated is gated
bool ConfigService::identity_refused_in_flight() {
    if (!identity_->activated() || on_ground()) return false;
    refuse(link::Reason::InFlight);
    return true;
}

void ConfigService::challenge() {
    if (identity_refused_in_flight()) return;
    char version[dfu::kVersionTextBytes] = {0};
    if (!identity_->unit().key_provisioned() || !running_version_text(version, sizeof(version)) ||
        !identity_->open(claim_.holder(), now_ms_)) {
        refuse(link::Reason::Keyless);
        return;
    }
    uint8_t frame[IdentityLink::kChallengeFrameBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Challenge);
    add_bytes<link::challenge_bytes::kNonce>(writer, link::challenge_key::kNonce,
                                             identity_->unit_nonce());
    add_bytes<link::challenge_bytes::kServerKeyId>(writer, link::challenge_key::kServerKeyId,
                                                   identity_->unit().server_key_id());
    const int bytes = writer.finish();
    if (bytes == 0) {
        diagnostics_.link_drops++;
        return;
    }
    (void)reply(frame, bytes);
}

void ConfigService::register_unit(const cbor::Reader& frame) {
    if (identity_refused_in_flight()) return;
    char version[dfu::kVersionTextBytes] = {0};
    if (!identity_->unit().key_provisioned() || !running_version_text(version, sizeof(version))) {
        refuse(link::Reason::Keyless);
        return;
    }
    if (!identity_->live(claim_.holder(), now_ms_)) {
        refuse(link::Reason::NoSession);
        return;
    }
    if (payload_bytes_to(claim_.holder()) < IdentityLink::kRegistrationFrameBytes) {
        refuse_too_wide();
        return;
    }
    identity::Nonce server_nonce{};
    crypto::Ed25519::Signature signature{};
    if (!read_bytes<link::register_bytes::kServerNonce>(frame, link::register_key::kServerNonce,
                                                        server_nonce) ||
        !read_bytes<link::register_bytes::kSignature>(frame, link::register_key::kSignature,
                                                      signature) ||
        !identity_->unit().registers(identity_->unit_nonce(), server_nonce, signature)) {
        refuse(link::Reason::BadSignature);
        return;
    }
    identity_->hold_server_nonce(server_nonce);
    stage(Pending::Register, link::Reason::ConfirmRegister);
}

void ConfigService::send_registration() {
    if (identity_ == nullptr || !identity_->registering(claim_.holder(), now_ms_)) {
        refuse(link::Reason::NoSession);
        return;
    }
    identity::Unit& unit = identity_->unit();
    char version[dfu::kVersionTextBytes] = {0};
    if (!running_version_text(version, sizeof(version))) {
        refuse(link::Reason::Keyless);
        return;
    }
    const bool release = dfu_ != nullptr && dfu_->release_build();
    const std::optional<crypto::Ed25519::Signature> signature =
        unit.sign(identity::registration_message(unit.device_id(), unit.public_key(),
                                                 identity_->unit_nonce(), identity_->server_nonce(),
                                                 unit.server_key_id(), release, version)
                      .bytes());
    if (!signature) {
        refuse(link::Reason::Keyless);
        return;
    }
    uint8_t frame[IdentityLink::kRegistrationFrameBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Registration);
    add_bytes<link::registration_bytes::kDeviceId>(writer, link::registration_key::kDeviceId,
                                                   unit.device_id());
    add_bytes<link::registration_bytes::kPublicKey>(writer, link::registration_key::kPublicKey,
                                                    unit.public_key());
    writer.add_text(link::registration_key::kVersion, version);
    add_bytes<link::registration_bytes::kServerKeyId>(writer, link::registration_key::kServerKeyId,
                                                      unit.server_key_id());
    writer.add_boolean(link::registration_key::kRelease, release);
    add_bytes<link::registration_bytes::kSignature>(writer, link::registration_key::kSignature,
                                                    *signature);
    const int bytes = writer.finish();
    if (bytes == 0) {
        diagnostics_.link_drops++;
        return;
    }
    (void)reply(frame, bytes);
}

// INFO: fc 18oct26 the nonce is spent before the check, so a refused code is never retried on it
void ConfigService::unlock(const cbor::Reader& frame) {
    if (identity_refused_in_flight()) return;
    if (!identity_->live(claim_.holder(), now_ms_)) {
        refuse(link::Reason::NoSession);
        return;
    }
    if (!settings_writable()) {
        refuse(link::Reason::LowPower);
        return;
    }
    identity::UnlockCode code{};
    code.nonce = identity_->unit_nonce();
    identity_->close();
    const Result<uint32_t> serial = frame.read_unsigned(link::unlock_key::kSerial);
    if (!serial.ok() || serial.value() < identity::kFirstSerial ||
        serial.value() > kSerialCeiling ||
        !read_bytes<link::unlock_bytes::kSignature>(frame, link::unlock_key::kSignature,
                                                    code.signature)) {
        refuse(link::Reason::BadSignature);
        return;
    }
    code.serial = static_cast<uint16_t>(serial.value());
    if (!identity_->unit().unlocks(code)) {
        refuse(link::Reason::BadSignature);
        return;
    }
    identity_->hold_unlock(code);
    if (!identity_->activated()) settle_unlock();
}

// INFO: fc 18oct26 a brown-out between erase and write costs one re-activation, to the same serial
void ConfigService::settle_unlock() {
    const std::optional<identity::UnlockCode> code = identity_->unlock_due();
    identity_->unlock_placed();
    if (!code) return;
    if (!settings_writable()) {
        refuse(link::Reason::LowPower);
        return;
    }
    if (identity_->unit().store(*code) != Status::Ok) {
        refuse(link::Reason::Flash);
        return;
    }
    acknowledge(true, link::Reason::Unlock);
}

}  // namespace skyblip::comms
