#ifndef SKYBLIP_CORE_COMMS_BROWSER_LINK_H
#define SKYBLIP_CORE_COMMS_BROWSER_LINK_H

#include <cstdint>
#include <cstring>

#include "core/comms/trusted_browsers.h"
#include "core/identity/messages.h"

namespace skyblip::comms {

// INFO: fc 13nov26 the nonce, proof and trust are the claim holder's, and its disconnect ends them
class BrowserLink {
   public:
    static constexpr int kChallengeFrameBytes = 22;
    static constexpr int kEntryFrameBytes = 45;

    TrustedBrowsers& trusted() { return trusted_; }
    const TrustedBrowsers& trusted() const { return trusted_; }

    void open(uint16_t session_id, const identity::Nonce& nonce) {
        forget(session_id_);
        session_id_ = session_id;
        nonce_ = nonce;
        nonce_live_ = true;
    }
    bool spend_nonce(uint16_t session_id, identity::Nonce& out) {
        if (!nonce_live_ || session_id != session_id_) return false;
        nonce_live_ = false;
        out = nonce_;
        return true;
    }

    void prove(const crypto::Ed25519::PublicKey& key, const char* label) {
        key_ = key;
        std::memcpy(label_, label, sizeof(label_));
        proven_ = true;
        trusted_session_ = trusted_.holds(key);
    }
    bool trusts(uint16_t session_id) const { return trusted_session_ && session_id == session_id_; }

    // INFO: fc 13nov26 two presses on a set or a dfu are what turn a proven key into a trusted one
    bool remember(uint16_t session_id) {
        if (!proven_ || session_id != session_id_ || trusted_.holds(key_)) return false;
        trusted_.store(key_, label_);
        trusted_session_ = true;
        return true;
    }

    bool forget_browser(const BrowserId& id) {
        if (!trusted_.forget(id)) return false;
        if (proven_ && !trusted_.holds(key_)) {
            proven_ = false;
            trusted_session_ = false;
        }
        return true;
    }

    void forget(uint16_t session_id) {
        if (session_id != session_id_) return;
        session_id_ = 0;
        nonce_live_ = false;
        proven_ = false;
        trusted_session_ = false;
    }

   private:
    TrustedBrowsers trusted_{};
    identity::Nonce nonce_{};
    crypto::Ed25519::PublicKey key_{};
    char label_[kBrowserLabelBytes + 1]{};
    uint16_t session_id_{0};
    bool nonce_live_{false};
    bool proven_{false};
    bool trusted_session_{false};
};

}  // namespace skyblip::comms

#endif
