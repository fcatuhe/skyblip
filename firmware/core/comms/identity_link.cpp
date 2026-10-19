#include "core/comms/identity_link.h"

#include <cstring>

#include "core/comms/config.h"
#include "core/dfu/update.h"
#include "core/util/hex.h"
#include "core/util/json_min.h"

namespace skyblip::comms {

namespace {
constexpr long kSerialCeiling = 0xFFFF;

template <size_t N>
bool read_hex_field(const json::Reader& r, const char* key, std::array<uint8_t, N>& out) {
    char text[2 * N + 2] = {0};
    return r.get_str(key, text, sizeof(text)) && hex::read(text, ByteSpan(out.data(), N));
}

template <size_t N>
void write_hex_field(json::Writer& w, const char* key, const std::array<uint8_t, N>& bytes) {
    char text[2 * N + 1];
    text[hex::write(ConstByteSpan(bytes.data(), N), text)] = 0;
    w.kv_str(key, text);
}
}  // namespace

// INFO: fc 18oct26 the server refuses a registration with no version, so none is ever sent
bool ConfigService::running_version_text(char* out, size_t cap) const {
    ports::ImageVersion running;
    if (dfu_ == nullptr || !dfu_->running_version(running)) return false;
    return dfu::format_version(running, out, cap) > 0;
}

bool IdentityLink::open(uint16_t session_id, uint32_t now_ms) {
    close();
    if (!unit_.draw_nonce(unit_nonce_)) return false;
    session_id_ = session_id;
    opened_ms_ = now_ms;
    open_ = true;
    return true;
}

// INFO: fc 18oct26 each refusal is refuse("word"): the server's activation page greps them
bool ConfigService::answer_identity(const char* cmd, const json::Reader& r) {
    if (identity_ == nullptr) return false;
    if (std::strcmp(cmd, "challenge") == 0) {
        challenge();
    } else if (std::strcmp(cmd, "register") == 0) {
        register_unit(r);
    } else if (std::strcmp(cmd, "unlock") == 0) {
        unlock(r);
    } else {
        return false;
    }
    return true;
}

// INFO: fc 18oct26 an unactivated unit is silent and a bench has no fix: only activated is gated
bool ConfigService::identity_refused_in_flight() {
    if (!identity_->activated() || on_ground()) return false;
    refuse("in_flight");
    return true;
}

void ConfigService::challenge() {
    if (identity_refused_in_flight()) return;
    char version[dfu::kVersionTextBytes] = {0};
    if (!identity_->unit().keyed() || !running_version_text(version, sizeof(version)) ||
        !identity_->open(claim_.holder(), now_ms_)) {
        refuse("keyless");
        return;
    }
    char buf[96];
    json::Writer w(buf, sizeof(buf));
    w.kv_str("cmd", "challenge");
    write_hex_field(w, "nonce", identity_->unit_nonce());
    write_hex_field(w, "server_key_id", identity_->unit().server_key_id());
    (void)reply(buf, w.finish());
}

void ConfigService::register_unit(const json::Reader& r) {
    if (identity_refused_in_flight()) return;
    char version[dfu::kVersionTextBytes] = {0};
    if (!identity_->unit().keyed() || !running_version_text(version, sizeof(version))) {
        refuse("keyless");
        return;
    }
    if (!identity_->live(claim_.holder(), now_ms_)) {
        refuse("no_session");
        return;
    }
    if (payload_to(claim_.holder()) < IdentityLink::kRegistrationFrameBytes) {
        refuse("small_mtu");
        return;
    }
    identity::Nonce server_nonce{};
    crypto::Ed25519::Signature signature{};
    if (!read_hex_field(r, "server_nonce", server_nonce) ||
        !read_hex_field(r, "signature", signature) ||
        !identity_->unit().registers(identity_->unit_nonce(), server_nonce, signature)) {
        refuse("bad_signature");
        return;
    }
    identity_->hold_server_nonce(server_nonce);
    stage(Pending::Register, "confirm_register");
}

void ConfigService::send_registration() {
    if (identity_ == nullptr || !identity_->registering(claim_.holder(), now_ms_)) {
        refuse("no_session");
        return;
    }
    identity::Unit& unit = identity_->unit();
    char version[dfu::kVersionTextBytes] = {0};
    if (!running_version_text(version, sizeof(version))) {
        refuse("keyless");
        return;
    }
    const bool release = dfu_ != nullptr && dfu_->release_build();
    const std::optional<crypto::Ed25519::Signature> signature =
        unit.sign(identity::registration_message(unit.device_id(), unit.public_key(),
                                                 identity_->unit_nonce(), identity_->server_nonce(),
                                                 unit.server_key_id(), release, version)
                      .bytes());
    if (!signature) {
        refuse("keyless");
        return;
    }
    char buf[IdentityLink::kRegistrationFrameBytes + 1];
    json::Writer w(buf, sizeof(buf));
    w.kv_str("cmd", "registration");
    write_hex_field(w, "device_id", unit.device_id());
    write_hex_field(w, "public_key", unit.public_key());
    w.kv_str("version", version);
    write_hex_field(w, "server_key_id", unit.server_key_id());
    w.kv_bool("release", release);
    write_hex_field(w, "signature", *signature);
    const int len = w.finish();
    if (w.overflowed()) {
        diagnostics_.link_drops++;
        return;
    }
    (void)reply(buf, len);
}

// INFO: fc 18oct26 the nonce is spent before the check, so a refused code is never retried on it
void ConfigService::unlock(const json::Reader& r) {
    if (identity_refused_in_flight()) return;
    if (!identity_->live(claim_.holder(), now_ms_)) {
        refuse("no_session");
        return;
    }
    if (!settings_writable()) {
        refuse("low_power");
        return;
    }
    identity::UnlockCode code{};
    code.nonce = identity_->unit_nonce();
    identity_->close();
    long serial = -1;
    if (!r.get_int("serial", serial) || serial < identity::kFirstSerial ||
        serial > kSerialCeiling || !read_hex_field(r, "signature", code.signature)) {
        refuse("bad_signature");
        return;
    }
    code.serial = static_cast<uint16_t>(serial);
    if (!identity_->unit().unlocks(code)) {
        refuse("bad_signature");
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
        refuse("low_power");
        return;
    }
    if (identity_->unit().store(*code) != Status::Ok) {
        refuse("flash");
        return;
    }
    ack(true, "unlock");
}

}  // namespace skyblip::comms
