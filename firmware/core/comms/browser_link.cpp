#include "core/comms/browser_link.h"

#include <cstring>

#include "core/comms/config.h"
#include "core/util/cbor.h"

namespace skyblip::comms {

namespace {
constexpr char kBrowserDomain[] = "skyblip:browser:v1";
constexpr size_t kDomainBytes = sizeof(kBrowserDomain) - 1;

using BrowserMessage = std::array<uint8_t, kDomainBytes + identity::kNonceBytes>;

BrowserMessage browser_message(const identity::Nonce& nonce) {
    BrowserMessage message{};
    std::memcpy(message.data(), kBrowserDomain, kDomainBytes);
    std::memcpy(message.data() + kDomainBytes, nonce.data(), nonce.size());
    return message;
}
}  // namespace

void ConfigService::browser_challenge() {
    identity::Nonce nonce{};
    if (!identity_->unit().draw_nonce(nonce)) {
        refuse(link::Reason::Keyless);
        return;
    }
    browsers_.open(claim_.peer(), nonce);
    uint8_t frame[BrowserLink::kChallengeFrameBytes];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::BrowserChallenge);
    writer.add_bytes(link::browser_challenge_key::kNonce,
                     ConstByteSpan(nonce.data(), nonce.size()));
    const int bytes = writer.finish();
    if (bytes == 0) {
        diagnostics_.link_drops++;
        return;
    }
    (void)reply(frame, bytes);
}

// INFO: fc 13nov26 the nonce is spent before the check, so a refused proof is never retried on it
void ConfigService::trust_browser(const cbor::Reader& frame) {
    identity::Nonce nonce{};
    if (!browsers_.spend_nonce(claim_.peer(), nonce)) {
        refuse(link::Reason::NoSession);
        return;
    }
    char label[kBrowserLabelBytes + 1]{};
    const Status labelled = frame.read_text(link::trust_browser_key::kLabel, label, sizeof(label));
    if (labelled != Status::Ok && labelled != Status::NotFound) {
        refuse(link::Reason::Invalid);
        return;
    }
    crypto::Ed25519::PublicKey key{};
    crypto::Ed25519::Signature signature{};
    static_assert(link::trust_browser_bytes::kKey == crypto::Ed25519::kPublicKeyBytes);
    static_assert(link::trust_browser_bytes::kSignature == crypto::Ed25519::kSignatureBytes);
    const BrowserMessage message = browser_message(nonce);
    if (frame.read_bytes(link::trust_browser_key::kKey, ByteSpan(key.data(), key.size())) !=
            Status::Ok ||
        frame.read_bytes(link::trust_browser_key::kSignature,
                         ByteSpan(signature.data(), signature.size())) != Status::Ok ||
        !crypto::Ed25519::verify(signature, key, ConstByteSpan(message.data(), message.size()))) {
        refuse(link::Reason::BadSignature);
        return;
    }
    browsers_.prove(key, label);
    if (trusted_browser())
        acknowledge(true, link::Reason::Trusted);
    else
        acknowledge(false, link::Reason::Untrusted);
}

void ConfigService::send_browser(const cbor::Reader& frame) {
    if (!trusted_browser()) {
        refuse(link::Reason::Untrusted);
        return;
    }
    const Result<uint32_t> index = frame.read_unsigned(link::browsers_key::kIndex);
    if (!index.ok()) {
        refuse(link::Reason::Invalid);
        return;
    }
    const TrustedBrowsers& trusted = browsers_.trusted();
    uint8_t reply_frame[BrowserLink::kEntryFrameBytes];
    cbor::Writer writer(reply_frame, sizeof(reply_frame));
    writer.add_code(link::kCommandKey, link::Command::Browser);
    writer.add_unsigned(link::browser_key::kIndex, index.value());
    writer.add_unsigned(link::browser_key::kCount, static_cast<uint32_t>(trusted.count()));
    if (index.value() < static_cast<uint32_t>(trusted.count())) {
        const TrustedBrowser& entry = trusted.at(static_cast<int>(index.value()));
        const BrowserId id = entry.id();
        writer.add_bytes(link::browser_key::kId, ConstByteSpan(id.data(), id.size()));
        writer.add_text(link::browser_key::kLabel, entry.label);
    }
    const int bytes = writer.finish();
    if (bytes == 0) {
        diagnostics_.link_drops++;
        return;
    }
    (void)reply(reply_frame, bytes);
}

void ConfigService::forget_browser(const cbor::Reader& frame) {
    if (!trusted_browser()) {
        refuse(link::Reason::Untrusted);
        return;
    }
    BrowserId id{};
    if (frame.read_bytes(link::forget_browser_key::kId, ByteSpan(id.data(), id.size())) !=
        Status::Ok) {
        refuse(link::Reason::Invalid);
        return;
    }
    if (!browsers_.forget_browser(id)) {
        acknowledge(false, link::Reason::UnknownBrowser);
        return;
    }
    browsers_dirty_ = true;
    acknowledge(true, link::Reason::Forgotten);
}

void ConfigService::remember_browser() {
    if (browsers_.remember(claim_.peer())) browsers_dirty_ = true;
}

}  // namespace skyblip::comms
